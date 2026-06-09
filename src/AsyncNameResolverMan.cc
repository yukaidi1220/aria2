/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2013 Tatsuhiro Tsujikawa
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
#include "AsyncNameResolverMan.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

#include "A2STR.h"
#include "AsyncNameResolver.h"
#include "AsyncResolver.h"
#ifdef ENABLE_SSL
#  include "AsyncDnsServerConfig.h"
#  include "AsyncDohNameResolver.h"
#  include "AsyncDotNameResolver.h"
#endif // ENABLE_SSL
#include "DownloadEngine.h"
#include "Command.h"
#include "message.h"
#include "fmt.h"
#include "LogFactory.h"
#include "Option.h"
#include "SocketCore.h"
#include "prefs.h"
#include "a2functional.h"
#include "util.h"

namespace aria2 {

namespace {
const char* resolverStatusToString(AsyncResolver::STATUS status)
{
  switch (status) {
  case AsyncResolver::STATUS_READY:
    return "ready";
  case AsyncResolver::STATUS_QUERYING:
    return "querying";
  case AsyncResolver::STATUS_SUCCESS:
    return "success";
  case AsyncResolver::STATUS_ERROR:
    return "error";
  default:
    return "unknown";
  }
}

const char* familyToString(int family)
{
  return family == AF_INET6 ? "AAAA" : "A";
}

const char* resolverModeToString(AsyncNameResolverMan::ResolverMode mode)
{
  switch (mode) {
  case AsyncNameResolverMan::RESOLVER_CARES:
    return "c-ares";
#ifdef ENABLE_SSL
  case AsyncNameResolverMan::RESOLVER_DOT:
    return "DoT";
  case AsyncNameResolverMan::RESOLVER_DOH:
    return "DoH";
  case AsyncNameResolverMan::RESOLVER_MULTI:
    return "multi";
#endif // ENABLE_SSL
  }
  abort();
}

int getBootstrapFamily(bool ipv4, bool ipv6)
{
  if (ipv4 && ipv6) {
    return AF_UNSPEC;
  }
  if (ipv6) {
    return AF_INET6;
  }
  if (ipv4) {
    return AF_INET;
  }
  return AF_UNSPEC;
}

AsyncNameResolverMan::ResolverMode resolverModeFromOption(const Option* option)
{
  const auto& mode = option->get(PREF_ASYNC_DNS_MODE);
  if (mode == V_CARES) {
    return AsyncNameResolverMan::RESOLVER_CARES;
  }
#ifdef ENABLE_SSL
  if (mode == V_DOT) {
    return AsyncNameResolverMan::RESOLVER_DOT;
  }
  if (mode == V_DOH) {
    return AsyncNameResolverMan::RESOLVER_DOH;
  }
  if (mode == V_MULTI) {
    return AsyncNameResolverMan::RESOLVER_MULTI;
  }
#endif // ENABLE_SSL
  abort();
}

#ifdef ENABLE_SSL
struct AsyncDnsMultiServerConfig {
  std::string udpServers;
  std::string tcpServers;
  std::vector<AsyncDnsServerConfig> dotServers;
  std::vector<AsyncDohServerConfig> dohServers;
};

bool startsWith(const std::string& s, const char* prefix)
{
  auto prefixLen = strlen(prefix);
  return s.size() >= prefixLen && s.compare(0, prefixLen, prefix) == 0;
}

void appendCsv(std::string& dst, const std::string& value)
{
  if (!dst.empty()) {
    dst += ",";
  }
  dst += value;
}

bool parsePlainDnsServer(std::string& host, std::string& port,
                         const std::string& server)
{
  if (server.empty()) {
    return false;
  }

  if (server[0] == '[') {
    auto closeBracket = server.find(']');
    if (closeBracket == std::string::npos || closeBracket == 1) {
      return false;
    }
    auto rest = util::strip(server.substr(closeBracket + 1));
    if (!rest.empty()) {
      if (rest[0] != ':') {
        return false;
      }
      port = util::strip(rest.substr(1));
      if (port.empty()) {
        return false;
      }
    }
    host = server.substr(1, closeBracket - 1);
    return true;
  }

  auto firstColon = server.find(':');
  if (firstColon != std::string::npos &&
      server.find(':', firstColon + 1) == std::string::npos) {
    host = util::strip(server.substr(0, firstColon));
    port = util::strip(server.substr(firstColon + 1));
    if (port.empty()) {
      return false;
    }
    return true;
  }

  host = server;
  return true;
}

bool isValidPlainDnsPort(const std::string& port)
{
  if (port.empty()) {
    return false;
  }

  uint32_t n;
  return util::parseUIntNoThrow(n, port) && n > 0 && n <= UINT16_MAX;
}

std::string normalizePlainDnsServer(const std::string& value)
{
  auto server = util::strip(value);
  std::string host;
  std::string port;
  if (!parsePlainDnsServer(host, port, server) || host.empty() ||
      !util::isNumericHost(host) ||
      (!port.empty() && !isValidPlainDnsPort(port))) {
    throw DL_ABORT_EX(
        fmt("Bad async DNS plain server '%s': expected a numeric address "
            "with optional port",
            value.c_str()));
  }
  return server;
}

AsyncDnsMultiServerConfig parseAsyncDnsMultiServerConfigList(
    const std::string& value)
{
  AsyncDnsMultiServerConfig config;
  if (value.empty()) {
    return config;
  }

  std::vector<std::string> entries;
  util::split(std::begin(value), std::end(value), std::back_inserter(entries),
              ',', true, true);
  for (auto entry : entries) {
    entry = util::strip(entry);
    if (entry.empty()) {
      continue;
    }
    if (startsWith(entry, "https://")) {
      config.dohServers.push_back(parseAsyncDnsDohServerConfig(entry));
    }
    else if (startsWith(entry, "dot://")) {
      config.dotServers.push_back(
          parseAsyncDnsDotServerConfig(entry.substr(strlen("dot://"))));
    }
    else if (startsWith(entry, "tcp://")) {
      appendCsv(config.tcpServers,
                normalizePlainDnsServer(entry.substr(strlen("tcp://"))));
    }
    else if (startsWith(entry, "udp://")) {
      appendCsv(config.udpServers,
                normalizePlainDnsServer(entry.substr(strlen("udp://"))));
    }
    else if (entry.find("://") != std::string::npos) {
      throw DL_ABORT_EX(
          fmt("Bad async DNS multi server '%s': unsupported scheme",
              entry.c_str()));
    }
    else {
      appendCsv(config.udpServers, normalizePlainDnsServer(entry));
    }
  }
  return config;
}
#endif // ENABLE_SSL
} // namespace

AsyncNameResolverMan::AsyncNameResolverMan()
    : resolverMode_(RESOLVER_CARES),
      dohHttp2_(false),
      ipv4_(true),
      ipv6_(true)
{
}

AsyncNameResolverMan::~AsyncNameResolverMan()
{
  assert(!resolverChecked());
}

bool AsyncNameResolverMan::started() const
{
  for (const auto& slot : resolverSlots_) {
    if (slot.resolver) {
      return true;
    }
  }
  return false;
}

bool AsyncNameResolverMan::resolverChecked() const
{
  for (const auto& slot : resolverSlots_) {
    if (slot.checked) {
      return true;
    }
  }
  return false;
}

void AsyncNameResolverMan::startAsync(const std::string& hostname,
                                      DownloadEngine* e, Command* command)
{
  assert(!resolverChecked());
  resolverSlots_.clear();
  A2_LOG_NETWORK(
      fmt("DNS: start resolving %s using %s", hostname.c_str(),
          resolverModeToString(resolverMode_)));
  A2_LOG_NETWORK(
      fmt("DNS: query families: A=%s, AAAA=%s",
          ipv4_ ? "yes" : "no", ipv6_ ? "yes" : "no"));
  if (!servers_.empty()) {
    A2_LOG_NETWORK(fmt("DNS: configured %s servers=%s",
                       resolverModeToString(resolverMode_), servers_.c_str()));
  }
  // Set IPv6 resolver first, so that we can push IPv6 address in
  // front of IPv6 address in getResolvedAddress().
  if (ipv6_) {
    startAsyncFamily(hostname, AF_INET6, e, command);
  }
  if (ipv4_) {
    startAsyncFamily(hostname, AF_INET, e, command);
  }
  A2_LOG_INFO(
      fmt(MSG_RESOLVING_HOSTNAME, command->getCuid(), hostname.c_str()));
}

std::shared_ptr<AsyncResolver> AsyncNameResolverMan::createResolver(
    int family) const
{
  switch (resolverMode_) {
  case RESOLVER_CARES:
    return std::make_shared<AsyncNameResolver>(family, servers_);
#ifdef ENABLE_SSL
  case RESOLVER_DOT: {
    auto dotServers = parseAsyncDnsDotServerConfigList(servers_);
    validateAsyncDnsDotServerConfig(dotServers);
    return std::make_shared<AsyncDotNameResolver>(
        family, std::move(dotServers), AsyncDotTransportFactory(),
        AsyncDotBootstrapResolverFactory(), getBootstrapFamily(ipv4_, ipv6_));
  }
  case RESOLVER_DOH: {
    auto dohServers = parseAsyncDnsDohServerConfigList(servers_);
    validateAsyncDnsDohServerConfig(dohServers);
    return std::make_shared<AsyncDohNameResolver>(
        family, std::move(dohServers), AsyncDohTransportFactory(), dohHttp2_,
        AsyncDohBootstrapResolverFactory(), getBootstrapFamily(ipv4_, ipv6_));
  }
  case RESOLVER_MULTI:
    return std::make_shared<AsyncNameResolver>(family, std::string());
#endif // ENABLE_SSL
  }
  abort();
}

std::vector<std::shared_ptr<AsyncResolver>>
AsyncNameResolverMan::createResolvers(int family) const
{
  std::vector<std::shared_ptr<AsyncResolver>> resolvers;
#ifdef ENABLE_SSL
  if (resolverMode_ == RESOLVER_MULTI) {
    auto config = parseAsyncDnsMultiServerConfigList(servers_);
    if (config.udpServers.empty() && config.tcpServers.empty()) {
      resolvers.push_back(
          std::make_shared<AsyncNameResolver>(family, std::string()));
    }
    else {
      if (!config.udpServers.empty()) {
        resolvers.push_back(
            std::make_shared<AsyncNameResolver>(family, config.udpServers));
      }
      if (!config.tcpServers.empty()) {
        resolvers.push_back(std::make_shared<AsyncNameResolver>(
            family, config.tcpServers, true));
      }
    }
    if (!config.dotServers.empty()) {
      resolvers.push_back(std::make_shared<AsyncDotNameResolver>(
          family, std::move(config.dotServers), AsyncDotTransportFactory(),
          AsyncDotBootstrapResolverFactory(), getBootstrapFamily(ipv4_, ipv6_)));
    }
    if (!config.dohServers.empty()) {
      resolvers.push_back(std::make_shared<AsyncDohNameResolver>(
          family, std::move(config.dohServers), AsyncDohTransportFactory(),
          dohHttp2_, AsyncDohBootstrapResolverFactory(),
          getBootstrapFamily(ipv4_, ipv6_)));
    }
    return resolvers;
  }
#endif // ENABLE_SSL
  resolvers.push_back(createResolver(family));
  return resolvers;
}

void AsyncNameResolverMan::startAsyncFamily(const std::string& hostname,
                                            int family, DownloadEngine* e,
                                            Command* command)
{
  auto resolvers = createResolvers(family);
  for (auto& resolver : resolvers) {
    resolver->resolve(hostname);
    resolverSlots_.push_back(ResolverSlot(std::move(resolver)));
    auto resolverIndex = resolverSlots_.size() - 1;
    if (resolverSlots_[resolverIndex].resolver->usable() && e && command) {
      setNameResolverCheck(resolverIndex, e, command);
    }
  }
}

void AsyncNameResolverMan::getResolvedAddress(
    std::vector<std::string>& res) const
{
  for (const auto& slot : resolverSlots_) {
    if (slot.resolver &&
        slot.resolver->getStatus() == AsyncResolver::STATUS_SUCCESS) {
      auto& addrs = slot.resolver->getResolvedAddresses();
      res.insert(std::end(res), std::begin(addrs), std::end(addrs));
    }
  }
  if (!res.empty()) {
    if (A2_LOG_NETWORK_ENABLED) {
      auto joined = strjoin(std::begin(res), std::end(res), ", ");
      A2_LOG_NETWORK(fmt("DNS: final address order: %s", joined.c_str()));
    }
  }
  return;
}

std::vector<std::shared_ptr<AsyncResolver>>
AsyncNameResolverMan::detachPendingResolvers(DownloadEngine* e,
                                             Command* command)
{
  std::vector<std::shared_ptr<AsyncResolver>> pendingResolvers;
  for (auto& slot : resolverSlots_) {
    auto& resolver = slot.resolver;
    if (!resolver ||
        (resolver->getStatus() != AsyncResolver::STATUS_READY &&
         resolver->getStatus() != AsyncResolver::STATUS_QUERYING) ||
        !resolver->usable()) {
      continue;
    }

    if (e && command && slot.checked) {
      e->deleteNameResolverCheck(resolver, command);
      slot.checked = false;
    }
    pendingResolvers.push_back(std::move(resolver));
  }
  return pendingResolvers;
}

void AsyncNameResolverMan::setNameResolverCheck(DownloadEngine* e,
                                                Command* command)
{
  for (size_t i = 0; i < resolverSlots_.size(); ++i) {
    setNameResolverCheck(i, e, command);
  }
}

void AsyncNameResolverMan::setNameResolverCheck(size_t index, DownloadEngine* e,
                                                Command* command)
{
  auto& slot = resolverSlots_[index];
  if (slot.resolver) {
    assert(!slot.checked);
    slot.checked = e->addNameResolverCheck(slot.resolver, command);
  }
}

void AsyncNameResolverMan::disableNameResolverCheck(DownloadEngine* e,
                                                    Command* command)
{
  for (size_t i = 0; i < resolverSlots_.size(); ++i) {
    disableNameResolverCheck(i, e, command);
  }
}

void AsyncNameResolverMan::disableNameResolverCheck(size_t index,
                                                    DownloadEngine* e,
                                                    Command* command)
{
  auto& slot = resolverSlots_[index];
  if (slot.resolver && slot.checked) {
    slot.checked = false;
    e->deleteNameResolverCheck(slot.resolver, command);
  }
}

int AsyncNameResolverMan::getStatus() const
{
  size_t success = 0;
  size_t error = 0;
  for (const auto& slot : resolverSlots_) {
    switch (slot.resolver->getStatus()) {
    case AsyncResolver::STATUS_SUCCESS:
      ++success;
      break;
    case AsyncResolver::STATUS_ERROR:
      ++error;
      break;
    default:
      break;
    }
  }
  if (success) {
    return 1;
  }
  else if (error == resolverSlots_.size()) {
    return -1;
  }
  else {
    return 0;
  }
}

const std::string& AsyncNameResolverMan::getLastError() const
{
  for (const auto& slot : resolverSlots_) {
    if (slot.resolver->getStatus() == AsyncResolver::STATUS_ERROR) {
      // TODO This is not last error chronologically.
      return slot.resolver->getError();
    }
  }
  return A2STR::NIL;
}

std::string AsyncNameResolverMan::getQueryStatus() const
{
  std::vector<std::string> entries;
  for (const auto& slot : resolverSlots_) {
    const auto& resolver = slot.resolver;
    if (!resolver) {
      continue;
    }
    auto entry = fmt("%s=%s", familyToString(resolver->getFamily()),
                     resolverStatusToString(resolver->getStatus()));
    if (resolver->getStatus() == AsyncResolver::STATUS_ERROR &&
        !resolver->getError().empty()) {
      entry += fmt("(%s)", resolver->getError().c_str());
    }
    entries.push_back(std::move(entry));
  }

  if (entries.empty()) {
    return A2STR::NIL;
  }
  return strjoin(std::begin(entries), std::end(entries), ", ");
}

void AsyncNameResolverMan::reset(DownloadEngine* e, Command* command)
{
  disableNameResolverCheck(e, command);
  assert(!resolverChecked());
  resolverSlots_.clear();
}

void validateAsyncNameResolverConfig(AsyncNameResolverMan::ResolverMode mode,
                                     const std::string& servers)
{
  switch (mode) {
  case AsyncNameResolverMan::RESOLVER_CARES:
    break;
#ifdef ENABLE_SSL
  case AsyncNameResolverMan::RESOLVER_DOT: {
    auto dotServers = parseAsyncDnsDotServerConfigList(servers);
    validateAsyncDnsDotServerConfig(dotServers);
    break;
  }
  case AsyncNameResolverMan::RESOLVER_DOH: {
    auto dohServers = parseAsyncDnsDohServerConfigList(servers);
    validateAsyncDnsDohServerConfig(dohServers);
    break;
  }
  case AsyncNameResolverMan::RESOLVER_MULTI: {
    (void)parseAsyncDnsMultiServerConfigList(servers);
    break;
  }
#endif // ENABLE_SSL
  }
}

void configureAsyncNameResolverMan(AsyncNameResolverMan* asyncNameResolverMan,
                                   Option* option)
{
  // Currently, aria2 checks configured addresses at the startup. But
  // there are chances that interfaces are not setup at that
  // moment. For example, if aria2 is used as daemon, it may start
  // before network interfaces up. To workaround this, we check
  // addresses again if both addresses are not configured at the
  // startup.
  if (!net::getIPv4AddrConfigured() && !net::getIPv6AddrConfigured()) {
    net::checkAddrconfig();
  }
  if (!net::getIPv4AddrConfigured()) {
    asyncNameResolverMan->setIPv4(false);
  }
  if (!net::getIPv6AddrConfigured() || option->getAsBool(PREF_DISABLE_IPV6)) {
    asyncNameResolverMan->setIPv6(false);
  }
  auto resolverMode = resolverModeFromOption(option);
  auto servers = option->get(PREF_ASYNC_DNS_SERVER);
  validateAsyncNameResolverConfig(resolverMode, servers);
  asyncNameResolverMan->setResolverMode(resolverMode);
  asyncNameResolverMan->setServers(std::move(servers));
  asyncNameResolverMan->setDohHttp2(
      option->getAsBool(PREF_ENABLE_HTTP2) &&
      !option->getAsBool(PREF_ENABLE_HTTP_PIPELINING));
}

} // namespace aria2
