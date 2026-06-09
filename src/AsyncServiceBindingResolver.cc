/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 Tatsuhiro Tsujikawa
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
 * file(s) with this exception, you may extend this exception statement to your
 * version, but you are not obligated to do so.  If you do not wish to do so,
 * delete this exception statement from your version.  If you delete this
 * exception statement from all source files in the program, then also delete
 * it here.
 */
/* copyright --> */
#include "AsyncServiceBindingResolver.h"

#include <algorithm>
#include <iterator>

#include "DlAbortEx.h"
#include "EventPoll.h"
#include "Exception.h"
#include "LogFactory.h"
#include "SocketCore.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace {
const uint16_t DEFAULT_HTTPS_PORT = 443;

uint16_t nextQueryId()
{
  static uint16_t queryId = 0;
  ++queryId;
  if (queryId == 0) {
    ++queryId;
  }
  return queryId;
}

bool hasMultipleServers(const std::string& servers)
{
  return servers.find(',') != std::string::npos;
}

int configureOptionsForServers(ares_options* opts, const std::string& servers)
{
  auto optmask = ARES_OPT_SOCK_STATE_CB;
  if (hasMultipleServers(servers)) {
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

std::string getSocketPeer(ares_socket_t fd)
{
  sockaddr_union su{};
  socklen_t len = sizeof(su);
  if (getpeername(fd, &su.sa, &len) == -1) {
    return std::string();
  }

  char host[NI_MAXHOST];
  char service[NI_MAXSERV];
  auto rv = getnameinfo(&su.sa, len, host, sizeof(host), service,
                        sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
  if (rv != 0) {
    return std::string();
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

int socket_cb(ares_socket_t fd, int type, void* /* userdata */)
{
  if (A2_LOG_NETWORK_ENABLED) {
    auto peer = getSocketPeer(fd);
    if (!peer.empty()) {
      A2_LOG_NETWORK(fmt("DNS: HTTPS RR c-ares socket connected to %s over %s",
                         peer.c_str(), socketTypeToString(type)));
    }
  }

  return ARES_SUCCESS;
}

void sock_state_cb(void* arg, ares_socket_t fd, int read, int write)
{
  auto resolver = static_cast<AsyncServiceBindingResolver*>(arg);

  resolver->handle_sock_state(fd, read, write);
}
} // namespace

std::string createHttpsServiceBindingQueryName(const std::string& hostname,
                                               uint16_t port)
{
  if (port == 0 || port == DEFAULT_HTTPS_PORT) {
    return hostname;
  }

  std::string queryName = "_";
  queryName += util::uitos(port);
  queryName += "._https.";
  queryName += hostname;
  return queryName;
}

void serviceBindingCallback(void* arg, int status, int timeouts,
                            unsigned char* abuf, int alen)
{
  auto resolver = reinterpret_cast<AsyncServiceBindingResolver*>(arg);
  const auto& hostname = resolver->getHostname();
  const auto& queryName = resolver->getQueryName();
  if (status != ARES_SUCCESS) {
    resolver->error_ = ares_strerror(status);
    resolver->status_ = AsyncResolver::STATUS_ERROR;
    if (timeouts > 0) {
      A2_LOG_NETWORK(
          fmt("DNS: HTTPS RR %s failed after %d c-ares timeout(s): %s",
              hostname.c_str(), timeouts, resolver->error_.c_str()));
    }
    else {
      A2_LOG_NETWORK(fmt("DNS: HTTPS RR %s failed: %s", hostname.c_str(),
                         resolver->error_.c_str()));
    }
    return;
  }

  if (alen < 0) {
    resolver->error_ = "bad DNS response length";
    resolver->status_ = AsyncResolver::STATUS_ERROR;
    A2_LOG_NETWORK(fmt("DNS: HTTPS RR %s failed: %s", hostname.c_str(),
                       resolver->error_.c_str()));
    return;
  }

  try {
    resolver->serviceBindingRecords_ = dns::parseServiceBindingResponse(
        abuf, static_cast<size_t>(alen), resolver->queryId_, queryName,
        dns::TYPE_HTTPS);
  }
  catch (Exception& e) {
    resolver->error_ = e.what();
    resolver->status_ = AsyncResolver::STATUS_ERROR;
    A2_LOG_NETWORK(fmt("DNS: HTTPS RR %s parse failed: %s", hostname.c_str(),
                       resolver->error_.c_str()));
    return;
  }

  resolver->status_ = AsyncResolver::STATUS_SUCCESS;
  A2_LOG_NETWORK(fmt("DNS: HTTPS RR %s returned %lu record(s)",
                     hostname.c_str(),
                     static_cast<unsigned long>(
                         resolver->serviceBindingRecords_.size())));
}

AsyncServiceBindingResolver::AsyncServiceBindingResolver(
    const std::string& servers, bool useTcp)
    : status_(STATUS_READY),
      channel_(nullptr),
      port_(DEFAULT_HTTPS_PORT),
      queryId_(0)
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
    A2_LOG_NETWORK(fmt("DNS: HTTPS RR %s", error_.c_str()));
    return;
  }
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS

  auto rv = ares_init_options(&channel_, &opts, optmask);
  if (rv != ARES_SUCCESS) {
    error_ = ares_strerror(rv);
    status_ = STATUS_ERROR;
    A2_LOG_NETWORK(fmt("DNS: HTTPS RR c-ares channel initialization failed: %s",
                       error_.c_str()));
    return;
  }

  ares_set_socket_callback(channel_, socket_cb, this);

  if (hasMultipleServers(servers)) {
#ifdef ARES_OPT_TIMEOUTMS
    A2_LOG_NETWORK(
        "DNS: HTTPS RR c-ares multi-server policy: timeout=2000ms, tries=2");
#else  // !ARES_OPT_TIMEOUTMS
    A2_LOG_NETWORK(
        "DNS: HTTPS RR c-ares multi-server policy: timeout=2s, tries=2");
#endif // !ARES_OPT_TIMEOUTMS
  }
  if (useTcp) {
    A2_LOG_NETWORK("DNS: HTTPS RR c-ares resolver forcing TCP transport");
  }

  if (!servers.empty()) {
    rv = ares_set_servers_csv(channel_, servers.c_str());
    if (rv != ARES_SUCCESS) {
      A2_LOG_DEBUG("ares_set_servers_csv failed");
      A2_LOG_NETWORK(
          fmt("DNS: HTTPS RR c-ares rejected configured server list %s: %s; "
              "keeping existing resolver configuration",
              servers.c_str(), ares_strerror(rv)));
    }
  }
}

AsyncServiceBindingResolver::~AsyncServiceBindingResolver()
{
  if (channel_) {
    ares_destroy(channel_);
  }
}

void AsyncServiceBindingResolver::resolve(const std::string& name)
{
  resolve(name, DEFAULT_HTTPS_PORT);
}

void AsyncServiceBindingResolver::resolve(const std::string& name,
                                          uint16_t port)
{
  hostname_ = name;
  port_ = port;
  queryName_ = createHttpsServiceBindingQueryName(hostname_, port_);
  serviceBindingRecords_.clear();
  resolvedAddresses_.clear();
  socks_.clear();

  if (!channel_) {
    if (error_.empty()) {
      error_ = "c-ares channel is not initialized";
    }
    status_ = STATUS_ERROR;
    A2_LOG_NETWORK(
        fmt("DNS: HTTPS RR query %s failed: %s", name.c_str(),
            error_.c_str()));
    return;
  }

  error_.clear();
  try {
    queryId_ = nextQueryId();
    auto query = dns::createQuery(queryId_, queryName_, dns::TYPE_HTTPS);
    status_ = STATUS_QUERYING;
    A2_LOG_NETWORK(fmt("DNS: query HTTPS RR %s using c-ares",
                       queryName_.c_str()));
    ares_send(channel_,
              reinterpret_cast<const unsigned char*>(query.data()),
              static_cast<int>(query.size()), serviceBindingCallback, this);
  }
  catch (Exception& e) {
    error_ = e.what();
    status_ = STATUS_ERROR;
    A2_LOG_NETWORK(fmt("DNS: HTTPS RR query %s failed: %s", name.c_str(),
                       error_.c_str()));
  }
}

const std::vector<AsyncResolverSocketEntry>&
AsyncServiceBindingResolver::getsock() const
{
  return socks_;
}

void AsyncServiceBindingResolver::process(sock_t readfd, sock_t writefd)
{
  if (!channel_) {
    return;
  }
  if (A2_LOG_NETWORK_ENABLED && readfd != badSocket()) {
    auto peer = getSocketPeer(toAresSocket(readfd));
    if (!peer.empty()) {
      A2_LOG_NETWORK(fmt("DNS: HTTPS RR c-ares socket readable from %s",
                         peer.c_str()));
    }
  }
  ares_process_fd(channel_, toAresSocket(readfd), toAresSocket(writefd));
}

void AsyncServiceBindingResolver::handle_sock_state(ares_socket_t fd, int read,
                                                    int write)
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

    socks_.push_back(AsyncResolverSocketEntry{sock, events});
    return;
  }

  if (!events) {
    socks_.erase(it);
    return;
  }

  (*it).events = events;
}

} // namespace aria2
