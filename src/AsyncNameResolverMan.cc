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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
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

std::string formatDnsLogHost(const std::string& host)
{
  if (host.find(':') != std::string::npos &&
      host.find(']') == std::string::npos) {
    std::string result = "[";
    result += host;
    result += "]";
    return result;
  }
  return host;
}

std::string formatCuid(Command* command)
{
  if (!command) {
    return "-";
  }
  return fmt("%" PRId64, command->getCuid());
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

class WindowedAsyncResolver : public AsyncResolver {
public:
  typedef std::function<std::shared_ptr<AsyncResolver>()> ResolverFactory;

private:
  int family_;
  std::string label_;
  std::vector<ResolverFactory> factories_;
  size_t activeLimit_;
  size_t nextFactory_;
  STATUS status_;
  std::string hostname_;
  std::string error_;
  std::vector<std::shared_ptr<AsyncResolver>> activeResolvers_;
  std::vector<std::string> resolvedAddresses_;
  std::vector<AsyncResolverSocketEntry> socks_;
  size_t startOffset_;

  bool socketBelongsTo(const AsyncResolver* resolver, sock_t fd) const
  {
    if (fd == badSocket()) {
      return false;
    }
    const auto& entries = resolver->getsock();
    return std::find_if(std::begin(entries), std::end(entries),
                        [fd](const AsyncResolverSocketEntry& entry) {
                          return entry.fd == fd;
                        }) != std::end(entries);
  }

  void updateSockets()
  {
    socks_.clear();
    for (const auto& resolver : activeResolvers_) {
      const auto& entries = resolver->getsock();
      socks_.insert(std::end(socks_), std::begin(entries), std::end(entries));
    }
  }

  void succeedFrom(const std::shared_ptr<AsyncResolver>& resolver)
  {
    resolvedAddresses_.clear();
    for (const auto& activeResolver : activeResolvers_) {
      if (activeResolver->getStatus() == STATUS_SUCCESS) {
        const auto& addrs = activeResolver->getResolvedAddresses();
        resolvedAddresses_.insert(std::end(resolvedAddresses_),
                                  std::begin(addrs), std::end(addrs));
      }
    }
    if (resolvedAddresses_.empty()) {
      const auto& addrs = resolver->getResolvedAddresses();
      resolvedAddresses_.insert(std::end(resolvedAddresses_), std::begin(addrs),
                                std::end(addrs));
    }
    status_ = STATUS_SUCCESS;
    activeResolvers_.clear();
    socks_.clear();
  }

  bool startOneResolver()
  {
    while (nextFactory_ < factories_.size()) {
      auto index = nextFactory_++;
      auto candidateIndex =
          factories_.empty() ? index : (startOffset_ + index) % factories_.size();
      try {
        auto resolver = factories_[index]();
        if (!resolver) {
          error_ = fmt("%s resolver factory returned null", label_.c_str());
          continue;
        }
        resolver->resolve(hostname_);
        A2_LOG_NETWORK(fmt("DNS: %s window activated candidate=%lu "
                           "active_limit=%lu",
                           label_.c_str(),
                           static_cast<unsigned long>(candidateIndex + 1),
                           static_cast<unsigned long>(activeLimit_)));
        activeResolvers_.push_back(std::move(resolver));
        return true;
      }
      catch (Exception& e) {
        error_ = e.what();
        A2_LOG_NETWORK(fmt("DNS: %s window candidate=%lu failed: %s",
                           label_.c_str(),
                           static_cast<unsigned long>(candidateIndex + 1),
                           error_.c_str()));
      }
    }
    return false;
  }

  void pump()
  {
    if (status_ != STATUS_QUERYING) {
      return;
    }

    bool changed = true;
    while (changed && status_ == STATUS_QUERYING) {
      changed = false;
      for (auto i = activeResolvers_.begin(); i != activeResolvers_.end();) {
        const auto status = (*i)->getStatus();
        if (status == STATUS_SUCCESS) {
          succeedFrom(*i);
          return;
        }
        if (status == STATUS_ERROR) {
          error_ = (*i)->getError();
          A2_LOG_NETWORK(fmt("DNS: %s window resolver failed: %s",
                             label_.c_str(), error_.c_str()));
          i = activeResolvers_.erase(i);
          changed = true;
          continue;
        }
        ++i;
      }
      while (status_ == STATUS_QUERYING &&
             activeResolvers_.size() < activeLimit_ &&
             nextFactory_ < factories_.size()) {
        changed = startOneResolver() || changed;
      }
    }

    if (status_ == STATUS_QUERYING && activeResolvers_.empty() &&
        nextFactory_ >= factories_.size()) {
      status_ = STATUS_ERROR;
      if (error_.empty()) {
        error_ = fmt("%s window exhausted", label_.c_str());
      }
    }
    updateSockets();
  }

public:
  WindowedAsyncResolver(int family, std::string label,
                        std::vector<ResolverFactory> factories,
                        size_t activeLimit, size_t startOffset)
      : family_(family),
        label_(std::move(label)),
        factories_(std::move(factories)),
        activeLimit_(std::max<size_t>(1, activeLimit)),
        nextFactory_(0),
        status_(STATUS_READY),
        startOffset_(0)
  {
    if (!factories_.empty()) {
      startOffset %= factories_.size();
      startOffset_ = startOffset;
      std::rotate(std::begin(factories_), std::begin(factories_) + startOffset,
                  std::end(factories_));
    }
  }

  virtual void resolve(const std::string& name) CXX11_OVERRIDE
  {
    hostname_ = name;
    error_.clear();
    resolvedAddresses_.clear();
    activeResolvers_.clear();
    socks_.clear();
    nextFactory_ = 0;
    status_ = factories_.empty() ? STATUS_ERROR : STATUS_QUERYING;
    if (status_ == STATUS_ERROR) {
      error_ = fmt("%s window has no resolver candidates", label_.c_str());
      return;
    }
    pump();
  }

  virtual const std::vector<std::string>&
  getResolvedAddresses() const CXX11_OVERRIDE
  {
    return resolvedAddresses_;
  }

  virtual const std::string& getError() const CXX11_OVERRIDE { return error_; }

  virtual STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  virtual bool usable() const CXX11_OVERRIDE
  {
    if (status_ != STATUS_QUERYING) {
      return false;
    }
    for (const auto& resolver : activeResolvers_) {
      if (resolver->usable()) {
        return true;
      }
    }
    return false;
  }

  virtual int getFamily() const CXX11_OVERRIDE { return family_; }

  virtual const std::vector<AsyncResolverSocketEntry>&
  getsock() const CXX11_OVERRIDE
  {
    return socks_;
  }

  virtual void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE
  {
    if (status_ != STATUS_QUERYING) {
      return;
    }
    if (readfd == badSocket() && writefd == badSocket()) {
      for (const auto& resolver : activeResolvers_) {
        resolver->process(readfd, writefd);
      }
      pump();
      return;
    }
    for (const auto& resolver : activeResolvers_) {
      auto childReadfd =
          socketBelongsTo(resolver.get(), readfd) ? readfd : badSocket();
      auto childWritefd =
          socketBelongsTo(resolver.get(), writefd) ? writefd : badSocket();
      if (childReadfd == badSocket() && childWritefd == badSocket()) {
        continue;
      }
      resolver->process(childReadfd, childWritefd);
    }
    pump();
  }

  virtual void processTimeout() CXX11_OVERRIDE
  {
    if (status_ != STATUS_QUERYING) {
      return;
    }
    for (const auto& resolver : activeResolvers_) {
      resolver->processTimeout();
    }
    pump();
  }

  virtual const std::string& getHostname() const CXX11_OVERRIDE
  {
    return hostname_;
  }
};

std::string formatDnsServerEndpoint(const std::string& host, uint16_t port)
{
  auto result = formatDnsLogHost(host);
  result += ":";
  result += util::uitos(port);
  return result;
}

std::string formatDotServerList(
    const std::vector<AsyncDnsServerConfig>& servers)
{
  std::vector<std::string> entries;
  for (const auto& server : servers) {
    auto entry = formatDnsServerEndpoint(server.connectHost, server.port);
    if (!server.tlsHost.empty() && server.tlsHost != server.connectHost) {
      entry += "#";
      entry += server.tlsHost;
    }
    entries.push_back(std::move(entry));
  }
  return strjoin(std::begin(entries), std::end(entries), ",");
}

std::string formatDohServerList(
    const std::vector<AsyncDohServerConfig>& servers)
{
  std::vector<std::string> entries;
  for (const auto& server : servers) {
    std::string entry = "https://";
    entry += formatDnsServerEndpoint(server.connectHost, server.port);
    entry += server.path;
    if (!server.tlsHost.empty() && server.tlsHost != server.connectHost) {
      entry += "#";
      entry += server.tlsHost;
    }
    entries.push_back(std::move(entry));
  }
  return strjoin(std::begin(entries), std::end(entries), ",");
}

bool needsBootstrap(const std::vector<AsyncDnsServerConfig>& servers)
{
  for (const auto& server : servers) {
    if (!util::isNumericHost(server.connectHost)) {
      return true;
    }
  }
  return false;
}

bool needsBootstrap(const std::vector<AsyncDohServerConfig>& servers)
{
  for (const auto& server : servers) {
    if (!util::isNumericHost(server.connectHost)) {
      return true;
    }
  }
  return false;
}

const char* dohTransportToString(bool dohHttp2)
{
#ifdef HAVE_LIBNGHTTP2
  return dohHttp2 ? "https-h1-or-h2" : "https-h1";
#else  // !HAVE_LIBNGHTTP2
  (void)dohHttp2;
  return "https-h1";
#endif // !HAVE_LIBNGHTTP2
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

} // namespace

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

std::vector<std::string> splitServerCsv(const std::string& servers)
{
  std::vector<std::string> entries;
  if (servers.empty()) {
    return entries;
  }
  util::split(std::begin(servers), std::end(servers),
              std::back_inserter(entries), ',', true, true);
  return entries;
}

size_t getMultiWindowLimit(bool ipv4, bool ipv6)
{
  return ipv4 && ipv6 ? 1 : 2;
}

size_t getMultiFamilyStartOffset(int family, bool ipv4, bool ipv6)
{
  return ipv4 && ipv6 && family == AF_INET ? 1 : 0;
}

std::vector<WindowedAsyncResolver::ResolverFactory> createPlainResolverFactories(
    int family, const AsyncDnsMultiServerConfig& config)
{
  auto udpServers = splitServerCsv(config.udpServers);
  auto tcpServers = splitServerCsv(config.tcpServers);
  std::vector<WindowedAsyncResolver::ResolverFactory> factories;
  auto maxLen = std::max(udpServers.size(), tcpServers.size());
  for (size_t i = 0; i < maxLen; ++i) {
    if (i < udpServers.size()) {
      auto server = udpServers[i];
      factories.push_back([family, server]() -> std::shared_ptr<AsyncResolver> {
        return std::make_shared<AsyncNameResolver>(family, server);
      });
    }
    if (i < tcpServers.size()) {
      auto server = tcpServers[i];
      factories.push_back([family, server]() -> std::shared_ptr<AsyncResolver> {
        return std::make_shared<AsyncNameResolver>(family, server, true);
      });
    }
  }
  return factories;
}

std::vector<WindowedAsyncResolver::ResolverFactory> createSecureResolverFactories(
    int family, const AsyncDnsMultiServerConfig& config, bool dohHttp2,
    std::function<std::unique_ptr<AsyncResolver>(int)> bootstrapFactory,
    int bootstrapFamily)
{
  std::vector<WindowedAsyncResolver::ResolverFactory> factories;
  auto maxLen = std::max(config.dotServers.size(), config.dohServers.size());
  for (size_t i = 0; i < maxLen; ++i) {
    if (i < config.dotServers.size()) {
      auto server = config.dotServers[i];
      factories.push_back([family, server, bootstrapFactory,
                           bootstrapFamily]() -> std::shared_ptr<AsyncResolver> {
        std::vector<AsyncDnsServerConfig> servers;
        servers.push_back(server);
        return std::make_shared<AsyncDotNameResolver>(
            family, std::move(servers), AsyncDotTransportFactory(),
            bootstrapFactory, bootstrapFamily);
      });
    }
    if (i < config.dohServers.size()) {
      auto server = config.dohServers[i];
      factories.push_back(
          [family, server, dohHttp2, bootstrapFactory,
           bootstrapFamily]() -> std::shared_ptr<AsyncResolver> {
            std::vector<AsyncDohServerConfig> servers;
            servers.push_back(server);
            return std::make_shared<AsyncDohNameResolver>(
                family, std::move(servers), AsyncDohTransportFactory(),
                dohHttp2, bootstrapFactory, bootstrapFamily);
          });
    }
  }
  return factories;
}

std::function<std::unique_ptr<AsyncResolver>(int)>
createPlainBootstrapResolverFactory(const AsyncDnsMultiServerConfig& config)
{
  if (config.udpServers.empty() && config.tcpServers.empty()) {
    return [](int family) {
      return make_unique<AsyncNameResolver>(family, std::string());
    };
  }
  return [config](int family) -> std::unique_ptr<AsyncResolver> {
    auto factories = createPlainResolverFactories(family, config);
    if (factories.empty()) {
      return make_unique<AsyncNameResolver>(family, std::string());
    }
    return make_unique<WindowedAsyncResolver>(
        family, "multi plain bootstrap", std::move(factories), 1, 0);
  };
}
#endif // ENABLE_SSL

AsyncNameResolverMan::AsyncNameResolverMan()
    : resolverMode_(RESOLVER_CARES),
      resolverPhase_(RESOLVER_PHASE_PRIMARY),
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
  hostname_ = hostname;
  resolverPhase_ = RESOLVER_PHASE_PRIMARY;
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
    const auto hasPlain =
        !config.udpServers.empty() || !config.tcpServers.empty();
    const auto hasSecure =
        !config.dotServers.empty() || !config.dohServers.empty();
    const auto windowLimit = getMultiWindowLimit(ipv4_, ipv6_);
    const auto startOffset = getMultiFamilyStartOffset(family, ipv4_, ipv6_);

    if (resolverPhase_ == RESOLVER_PHASE_PRIMARY && hasSecure) {
      auto plainBootstrapResolverFactory =
          createPlainBootstrapResolverFactory(config);
      auto factories = createSecureResolverFactories(
          family, config, dohHttp2_, plainBootstrapResolverFactory,
          getBootstrapFamily(ipv4_, ipv6_));
      resolvers.push_back(std::make_shared<WindowedAsyncResolver>(
          family, "multi secure DNS", std::move(factories), windowLimit,
          startOffset));
      return resolvers;
    }

    if ((resolverPhase_ == RESOLVER_PHASE_PRIMARY && !hasSecure) ||
        resolverPhase_ == RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK) {
      auto factories = createPlainResolverFactories(family, config);
      if (!factories.empty()) {
        resolvers.push_back(std::make_shared<WindowedAsyncResolver>(
            family, "multi plain DNS", std::move(factories), windowLimit,
            startOffset));
      }
      if (!resolvers.empty()) {
        return resolvers;
      }
    }

    if (resolverPhase_ == RESOLVER_PHASE_SYSTEM_CARES_FALLBACK ||
        (resolverPhase_ == RESOLVER_PHASE_PRIMARY && !hasPlain &&
         !hasSecure)) {
      resolvers.push_back(
          std::make_shared<AsyncNameResolver>(family, std::string()));
    }
    return resolvers;
  }
#endif // ENABLE_SSL
  if (resolverPhase_ == RESOLVER_PHASE_SYSTEM_CARES_FALLBACK) {
    resolvers.push_back(
        std::make_shared<AsyncNameResolver>(family, std::string()));
    return resolvers;
  }
  resolvers.push_back(createResolver(family));
  return resolvers;
}

const char*
AsyncNameResolverMan::resolverPhaseToString(ResolverPhase phase) const
{
  switch (phase) {
  case RESOLVER_PHASE_PRIMARY:
    return "primary";
  case RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK:
    return "explicit-plain-fallback";
  case RESOLVER_PHASE_SYSTEM_CARES_FALLBACK:
    return "system-cares-fallback";
  }
  abort();
}

void AsyncNameResolverMan::logResolverPlan(const std::string& hostname,
                                           int family, Command* command) const
{
  if (!A2_LOG_NETWORK_ENABLED) {
    return;
  }

  const auto qtype = familyToString(family);
  const auto phase = resolverPhaseToString(resolverPhase_);
  const auto mode = resolverModeToString(resolverMode_);
  const auto cuid = formatCuid(command);
  auto logPlan = [&](const char* backend, const char* transport,
                     const char* server, const char* bootstrap,
                     const char* fallbackFrom) {
    A2_LOG_NETWORK(
        fmt("DNS: CUID#%s - query plan host=%s qtype=%s mode=%s phase=%s "
            "backend=%s transport=%s server=%s bootstrap=%s "
            "fallback_from=%s",
            cuid.c_str(), hostname.c_str(), qtype, mode, phase, backend,
            transport, server && *server ? server : "-",
            bootstrap && *bootstrap ? bootstrap : "none",
            fallbackFrom && *fallbackFrom ? fallbackFrom : "none"));
  };

  if (resolverPhase_ == RESOLVER_PHASE_SYSTEM_CARES_FALLBACK) {
    const char* fallbackFrom = "primary";
    if (resolverMode_ == RESOLVER_CARES && !servers_.empty()) {
      fallbackFrom = "explicit-cares-dns";
    }
#ifdef ENABLE_SSL
    if (resolverMode_ == RESOLVER_DOT || resolverMode_ == RESOLVER_DOH) {
      fallbackFrom = "secure-dns";
    }
    else if (resolverMode_ == RESOLVER_MULTI) {
      auto config = parseAsyncDnsMultiServerConfigList(servers_);
      if (!config.udpServers.empty() || !config.tcpServers.empty()) {
        fallbackFrom = "explicit-plain-dns";
      }
      else if (!config.dotServers.empty() || !config.dohServers.empty()) {
        fallbackFrom = "secure-dns";
      }
    }
#endif // ENABLE_SSL
    logPlan("c-ares", "system-cares", "system", "none", fallbackFrom);
    return;
  }

  if (resolverMode_ == RESOLVER_CARES) {
    logPlan("c-ares", servers_.empty() ? "system-cares" : "udp",
            servers_.empty() ? "system" : servers_.c_str(), "none", "none");
    return;
  }

#ifdef ENABLE_SSL
  if (resolverMode_ == RESOLVER_DOT) {
    auto dotServers = parseAsyncDnsDotServerConfigList(servers_);
    auto formatted = formatDotServerList(dotServers);
    logPlan("DoT", "tls", formatted.c_str(),
            needsBootstrap(dotServers) ? "system-cares" : "none", "none");
    return;
  }

  if (resolverMode_ == RESOLVER_DOH) {
    auto dohServers = parseAsyncDnsDohServerConfigList(servers_);
    auto formatted = formatDohServerList(dohServers);
    logPlan("DoH", dohTransportToString(dohHttp2_), formatted.c_str(),
            needsBootstrap(dohServers) ? "system-cares" : "none", "none");
    return;
  }

  if (resolverMode_ == RESOLVER_MULTI) {
    auto config = parseAsyncDnsMultiServerConfigList(servers_);
    if (resolverPhase_ == RESOLVER_PHASE_PRIMARY) {
      if (!config.dotServers.empty()) {
        auto formatted = formatDotServerList(config.dotServers);
        const char* bootstrap =
            needsBootstrap(config.dotServers)
                ? (!config.udpServers.empty() || !config.tcpServers.empty()
                       ? "explicit-plain-dns"
                       : "system-cares")
                : "none";
        logPlan("DoT", "tls", formatted.c_str(), bootstrap, "none");
      }
      if (!config.dohServers.empty()) {
        auto formatted = formatDohServerList(config.dohServers);
        const char* bootstrap =
            needsBootstrap(config.dohServers)
                ? (!config.udpServers.empty() || !config.tcpServers.empty()
                       ? "explicit-plain-dns"
                       : "system-cares")
                : "none";
        logPlan("DoH", dohTransportToString(dohHttp2_), formatted.c_str(),
                bootstrap, "none");
      }
      if (config.dotServers.empty() && config.dohServers.empty()) {
        if (!config.udpServers.empty()) {
          logPlan("c-ares", "udp", config.udpServers.c_str(), "none", "none");
        }
        if (!config.tcpServers.empty()) {
          logPlan("c-ares", "tcp", config.tcpServers.c_str(), "none", "none");
        }
        if (config.udpServers.empty() && config.tcpServers.empty()) {
          logPlan("c-ares", "system-cares", "system", "none", "none");
        }
      }
      return;
    }

    if (resolverPhase_ == RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK) {
      if (!config.udpServers.empty()) {
        logPlan("c-ares", "udp", config.udpServers.c_str(), "none",
                "secure-dns");
      }
      if (!config.tcpServers.empty()) {
        logPlan("c-ares", "tcp", config.tcpServers.c_str(), "none",
                "secure-dns");
      }
      return;
    }
  }
#endif // ENABLE_SSL
}

void AsyncNameResolverMan::startAsyncFamily(const std::string& hostname,
                                            int family, DownloadEngine* e,
                                            Command* command)
{
  logResolverPlan(hostname, family, command);
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
  size_t active = 0;
  for (const auto& slot : resolverSlots_) {
    if (!slot.resolver) {
      continue;
    }
    ++active;
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
  else if (active == 0 || error == active) {
    return -1;
  }
  else {
    return 0;
  }
}

const std::string& AsyncNameResolverMan::getLastError() const
{
  for (const auto& slot : resolverSlots_) {
    if (!slot.resolver) {
      continue;
    }
    if (slot.resolver->getStatus() == AsyncResolver::STATUS_ERROR) {
      // TODO This is not last error chronologically.
      return slot.resolver->getError();
    }
  }
  return A2STR::NIL;
}

bool AsyncNameResolverMan::getNextFallbackPhase(ResolverPhase& nextPhase) const
{
#ifdef ENABLE_SSL
  if ((resolverMode_ == RESOLVER_DOT || resolverMode_ == RESOLVER_DOH) &&
      resolverPhase_ == RESOLVER_PHASE_PRIMARY) {
    nextPhase = RESOLVER_PHASE_SYSTEM_CARES_FALLBACK;
    return true;
  }

  if (resolverMode_ == RESOLVER_MULTI) {
    auto config = parseAsyncDnsMultiServerConfigList(servers_);
    const auto hasPlain =
        !config.udpServers.empty() || !config.tcpServers.empty();
    const auto hasSecure =
        !config.dotServers.empty() || !config.dohServers.empty();
    if (hasSecure && resolverPhase_ == RESOLVER_PHASE_PRIMARY && hasPlain) {
      nextPhase = RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK;
      return true;
    }
    if (hasSecure &&
        (resolverPhase_ == RESOLVER_PHASE_PRIMARY ||
         resolverPhase_ == RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK)) {
      nextPhase = RESOLVER_PHASE_SYSTEM_CARES_FALLBACK;
      return true;
    }
    if (!hasSecure && hasPlain && resolverPhase_ == RESOLVER_PHASE_PRIMARY) {
      nextPhase = RESOLVER_PHASE_SYSTEM_CARES_FALLBACK;
      return true;
    }
  }
#endif // ENABLE_SSL
  if (resolverMode_ == RESOLVER_CARES && !servers_.empty() &&
      resolverPhase_ == RESOLVER_PHASE_PRIMARY) {
    nextPhase = RESOLVER_PHASE_SYSTEM_CARES_FALLBACK;
    return true;
  }
  return false;
}

bool AsyncNameResolverMan::startFallback(DownloadEngine* e, Command* command)
{
  ResolverPhase nextPhase;
  if (hostname_.empty() || getStatus() != -1) {
    return false;
  }
  if (!getNextFallbackPhase(nextPhase)) {
    return false;
  }

  const auto previousPhase = resolverPhase_;
  disableNameResolverCheck(e, command);
  assert(!resolverChecked());
  resolverSlots_.clear();
  resolverPhase_ = nextPhase;

  const char* nextName =
      nextPhase == RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK
          ? "explicit plain DNS"
          : "system c-ares DNS";
  const char* previousName = "explicit DNS";
#ifdef ENABLE_SSL
  if (previousPhase == RESOLVER_PHASE_PRIMARY) {
    if (resolverMode_ == RESOLVER_DOT || resolverMode_ == RESOLVER_DOH) {
      previousName = "secure DNS";
    }
    else if (resolverMode_ == RESOLVER_MULTI) {
      auto config = parseAsyncDnsMultiServerConfigList(servers_);
      if (!config.dotServers.empty() || !config.dohServers.empty()) {
        previousName = "secure DNS";
      }
      else if (!config.udpServers.empty() || !config.tcpServers.empty()) {
        previousName = "explicit plain DNS";
      }
    }
  }
#endif // ENABLE_SSL
  if (previousPhase == RESOLVER_PHASE_PRIMARY &&
      resolverMode_ == RESOLVER_CARES && !servers_.empty()) {
    previousName = "explicit c-ares DNS";
  }
  if (previousPhase == RESOLVER_PHASE_EXPLICIT_PLAIN_FALLBACK) {
    previousName = "explicit plain DNS";
  }
  A2_LOG_NETWORK(fmt("DNS: %s failed; falling back to %s for %s",
                     previousName, nextName, hostname_.c_str()));
  if (ipv6_) {
    startAsyncFamily(hostname_, AF_INET6, e, command);
  }
  if (ipv4_) {
    startAsyncFamily(hostname_, AF_INET, e, command);
  }
  return true;
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
  hostname_.clear();
  resolverPhase_ = RESOLVER_PHASE_PRIMARY;
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
  if (!option->getAsBool(PREF_ASYNC_DNS)) {
    A2_LOG_NETWORK(
        "DNS: async DNS disabled; secure DNS resolver config is ignored");
    return;
  }

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
      (option->getAsBool(PREF_ENABLE_HTTP2) ||
       option->getAsBool(PREF_ENABLE_DOH_HTTP2)) &&
      !option->getAsBool(PREF_ENABLE_HTTP_PIPELINING));
}

} // namespace aria2
