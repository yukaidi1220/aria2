/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "AsyncNameResolver.h"

#include <cstring>

#include "A2STR.h"
#include "LogFactory.h"
#include "SocketCore.h"
#include "util.h"
#include "EventPoll.h"
#include "fmt.h"

namespace aria2 {

namespace {
const char* familyToString(int family)
{
  if (family == AF_INET6) {
    return "AAAA";
  }
  if (family == AF_INET) {
    return "A";
  }
  return "A/AAAA";
}

const char* socketTypeToString(int type)
{
  switch (type) {
  case SOCK_DGRAM:
    return "UDP";
  case SOCK_STREAM:
    return "TCP";
  default:
    return "unknown transport";
  }
}

bool hasMultipleServers(const std::string& servers)
{
  return servers.find(',') != std::string::npos;
}

ares_socket_t toAresSocket(sock_t fd)
{
  if (fd == AsyncResolver::badSocket()) {
    return ARES_SOCKET_BAD;
  }
  return static_cast<ares_socket_t>(fd);
}

sock_t fromAresSocket(ares_socket_t fd)
{
  if (fd == ARES_SOCKET_BAD) {
    return AsyncResolver::badSocket();
  }
  return static_cast<sock_t>(fd);
}

int configureOptionsForServers(ares_options* opts, const std::string& servers)
{
  auto optmask = ARES_OPT_SOCK_STATE_CB;
  if (hasMultipleServers(servers)) {
    // Keep c-ares' per-server retry cycle below aria2's outer DNS timeout, so
    // a blackholed first server still leaves time to try later servers.
    opts->tries = 2;
    optmask |= ARES_OPT_TRIES;
#ifdef ARES_OPT_TIMEOUTMS
    opts->timeout = 2000;
    optmask |= ARES_OPT_TIMEOUTMS;
#else  // !ARES_OPT_TIMEOUTMS
    opts->timeout = 2;
    optmask |= ARES_OPT_TIMEOUT;
#endif // !ARES_OPT_TIMEOUTMS
  }
  return optmask;
}

std::string getSocketPeer(ares_socket_t fd)
{
  sockaddr_union su{};
  socklen_t len = sizeof(su);
  if (getpeername(fd, &su.sa, &len) == -1) {
    return A2STR::NIL;
  }

  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  auto rv = getnameinfo(&su.sa, len, host, sizeof(host), service,
                        sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
  if (rv != 0) {
    return A2STR::NIL;
  }

  std::string peer;
  if (su.sa.sa_family == AF_INET6) {
    peer += "[";
    peer += host;
    peer += "]";
  }
  else {
    peer += host;
  }
  peer += ":";
  peer += service;
  return peer;
}

int socket_cb(ares_socket_t fd, int type, void* /* userdata */)
{
  if (A2_LOG_NETWORK_ENABLED) {
    auto peer = getSocketPeer(fd);
    if (!peer.empty()) {
      A2_LOG_NETWORK(fmt("DNS: c-ares socket connected to %s over %s",
                         peer.c_str(), socketTypeToString(type)));
    }
  }

  return ARES_SUCCESS;
}

#ifdef HAVE_ARES_SET_SERVER_STATE_CALLBACK
const char* serverStateTransportToString(int flags)
{
  if (flags & ARES_SERV_STATE_TCP) {
    return "TCP";
  }
  if (flags & ARES_SERV_STATE_UDP) {
    return "UDP";
  }
  return "unknown transport";
}

void server_state_cb(const char* serverString, ares_bool_t success, int flags,
                     void* data)
{
  auto resolver = static_cast<AsyncNameResolver*>(data);
  auto famStr = familyToString(resolver->getFamily());
  auto hostname = resolver->getHostname();
  if (hostname.empty()) {
    hostname = "(pending)";
  }
  A2_LOG_NETWORK(
      fmt("DNS: c-ares server %s %s for %s %s over %s",
          serverString ? serverString : "(unknown)",
          success == ARES_TRUE ? "succeeded" : "failed", famStr,
          hostname.c_str(), serverStateTransportToString(flags)));
}
#endif // HAVE_ARES_SET_SERVER_STATE_CALLBACK
} // namespace

void callback(void* arg, int status, int timeouts, ares_addrinfo* result)
{
  AsyncNameResolver* resolverPtr = reinterpret_cast<AsyncNameResolver*>(arg);
  const auto& hostname = resolverPtr->getHostname();
  const auto famStr = familyToString(resolverPtr->getFamily());
  if (status != ARES_SUCCESS) {
    resolverPtr->error_ = ares_strerror(status);
    resolverPtr->status_ = AsyncNameResolver::STATUS_ERROR;
    if (timeouts > 0) {
      A2_LOG_NETWORK(fmt("DNS: %s %s failed after %d c-ares timeout(s): %s",
                         famStr, hostname.c_str(), timeouts,
                         resolverPtr->error_.c_str()));
    }
    else {
      A2_LOG_NETWORK(
          fmt("DNS: %s %s failed: %s", famStr, hostname.c_str(),
              resolverPtr->error_.c_str()));
    }
    return;
  }
  for (auto ap = result->nodes; ap; ap = ap->ai_next) {
    char addrstring[NI_MAXHOST];
    auto rv = getnameinfo(ap->ai_addr, ap->ai_addrlen, addrstring,
                          sizeof(addrstring), nullptr, 0, NI_NUMERICHOST);
    if (rv == 0) {
      resolverPtr->resolvedAddresses_.push_back(addrstring);
    }
  }
  ares_freeaddrinfo(result);
  if (resolverPtr->resolvedAddresses_.empty()) {
    resolverPtr->error_ = "no address returned or address conversion failed";
    resolverPtr->status_ = AsyncNameResolver::STATUS_ERROR;
    A2_LOG_NETWORK(
        fmt("DNS: %s %s failed: %s", famStr, hostname.c_str(),
            resolverPtr->error_.c_str()));
  }
  else {
    resolverPtr->status_ = AsyncNameResolver::STATUS_SUCCESS;
    if (A2_LOG_NETWORK_ENABLED) {
      auto addrs = strjoin(std::begin(resolverPtr->resolvedAddresses_),
                           std::end(resolverPtr->resolvedAddresses_), ", ");
      A2_LOG_NETWORK(
          fmt("DNS: %s %s -> %s", famStr, hostname.c_str(), addrs.c_str()));
    }
  }
}

namespace {
void sock_state_cb(void* arg, ares_socket_t fd, int read, int write)
{
  auto resolver = static_cast<AsyncNameResolver*>(arg);

  resolver->handle_sock_state(fd, read, write);
}
} // namespace

void AsyncNameResolver::handle_sock_state(ares_socket_t fd, int read, int write)
{
  auto sock = fromAresSocket(fd);
  int events = 0;

  if (read) {
    events |= EventPoll::EVENT_READ;
  }

  if (write) {
    events |= EventPoll::EVENT_WRITE;
  }

  auto it = std::find_if(
      std::begin(socks_), std::end(socks_),
      [sock](const AsyncResolverSocketEntry& ent) { return ent.fd == sock; });
  if (it == std::end(socks_)) {
    if (!events) {
      return;
    }

    socks_.emplace_back(AsyncResolverSocketEntry{sock, events});

    return;
  }

  if (!events) {
    socks_.erase(it);
    return;
  }

  (*it).events = events;
}

AsyncNameResolver::AsyncNameResolver(int family, const std::string& servers,
                                     bool useTcp)
    : status_(STATUS_READY), family_(family), channel_(nullptr)
{
  ares_options opts{};
  opts.sock_state_cb = sock_state_cb;
  opts.sock_state_cb_data = this;

  auto optmask = configureOptionsForServers(&opts, servers);
#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
  if (useTcp) {
    opts.flags |= ARES_FLAG_USEVC;
    optmask |= ARES_OPT_FLAGS;
  }
#else  // !(ARES_FLAG_USEVC && ARES_OPT_FLAGS)
  if (useTcp) {
    error_ = "c-ares TCP transport is not supported by this build";
    status_ = STATUS_ERROR;
    A2_LOG_NETWORK(fmt("DNS: %s", error_.c_str()));
    return;
  }
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS
  auto rv = ares_init_options(&channel_, &opts, optmask);
  if (rv != ARES_SUCCESS) {
    error_ = ares_strerror(rv);
    status_ = STATUS_ERROR;
    A2_LOG_NETWORK(
        fmt("DNS: c-ares channel initialization failed: %s", error_.c_str()));
    return;
  }

  ares_set_socket_callback(channel_, socket_cb, this);
#ifdef HAVE_ARES_SET_SERVER_STATE_CALLBACK
  ares_set_server_state_callback(channel_, server_state_cb, this);
#endif // HAVE_ARES_SET_SERVER_STATE_CALLBACK

  if (hasMultipleServers(servers)) {
#ifdef ARES_OPT_TIMEOUTMS
    A2_LOG_NETWORK(
        "DNS: c-ares multi-server policy: timeout=2000ms, tries=2");
#else  // !ARES_OPT_TIMEOUTMS
    A2_LOG_NETWORK("DNS: c-ares multi-server policy: timeout=2s, tries=2");
#endif // !ARES_OPT_TIMEOUTMS
  }
  if (useTcp) {
    A2_LOG_NETWORK("DNS: c-ares plain resolver forcing TCP transport");
  }

  if (!servers.empty()) {
    rv = ares_set_servers_csv(channel_, servers.c_str());
    if (rv != ARES_SUCCESS) {
      A2_LOG_DEBUG("ares_set_servers_csv failed");
      A2_LOG_NETWORK(
          fmt("DNS: c-ares rejected configured server list %s: %s; keeping "
              "existing resolver configuration",
              servers.c_str(), ares_strerror(rv)));
    }
  }
}

AsyncNameResolver::~AsyncNameResolver()
{
  if (channel_) {
    ares_destroy(channel_);
  }
}

void AsyncNameResolver::resolve(const std::string& name)
{
  hostname_ = name;
  if (!channel_) {
    A2_LOG_NETWORK(
        fmt("DNS: query %s failed: c-ares channel is not initialized",
            name.c_str()));
    return;
  }
  status_ = STATUS_QUERYING;

  ares_addrinfo_hints hints{};
  hints.ai_family = family_;

  const auto famStr = familyToString(family_);
  A2_LOG_NETWORK(fmt("DNS: query %s %s using c-ares", famStr, name.c_str()));
  ares_getaddrinfo(channel_, name.c_str(), nullptr, &hints, callback, this);
}

const std::vector<AsyncResolverSocketEntry>& AsyncNameResolver::getsock() const
{
  return socks_;
}

void AsyncNameResolver::process(sock_t readfd, sock_t writefd)
{
  if (!channel_) {
    return;
  }
  if (A2_LOG_NETWORK_ENABLED && readfd != badSocket()) {
    auto peer = getSocketPeer(toAresSocket(readfd));
    if (!peer.empty()) {
      A2_LOG_NETWORK(
          fmt("DNS: c-ares socket readable from %s", peer.c_str()));
    }
  }
  ares_process_fd(channel_, toAresSocket(readfd), toAresSocket(writefd));
}

bool AsyncNameResolver::operator==(const AsyncNameResolver& resolver) const
{
  return this == &resolver;
}

} // namespace aria2
