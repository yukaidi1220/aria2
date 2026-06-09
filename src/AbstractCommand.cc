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
#include "AbstractCommand.h"

#include <algorithm>
#include <functional>
#include <limits>

#include "Request.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "PeerStat.h"
#include "SegmentMan.h"
#include "Logger.h"
#include "Segment.h"
#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "DownloadFailureException.h"
#include "CreateRequestCommand.h"
#include "InitiateConnectionCommandFactory.h"
#include "StreamCheckIntegrityEntry.h"
#include "PieceStorage.h"
#include "SocketCore.h"
#include "HostMapping.h"
#include "message.h"
#include "prefs.h"
#include "fmt.h"
#include "ServerStat.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "A2STR.h"
#include "util.h"
#include "LogFactory.h"
#include "DownloadContext.h"
#include "GroupId.h"
#include "wallclock.h"
#include "NameResolver.h"
#include "uri.h"
#include "FileEntry.h"
#include "error_code.h"
#include "SocketRecvBuffer.h"
#include "ChecksumCheckIntegrityEntry.h"
#ifdef ENABLE_ASYNC_DNS
#  include "AsyncNameResolver.h"
#  include "AsyncNameResolverMan.h"
#  include "AsyncServiceBindingResolver.h"
#  ifdef ENABLE_SSL
#    include "AsyncDohNameResolver.h"
#    include "AsyncDotNameResolver.h"
#  endif // ENABLE_SSL
#endif // ENABLE_ASYNC_DNS

namespace aria2 {

namespace {
bool shouldWaitBeforeRetry(error_code::Value code)
{
  switch (code) {
  case error_code::HTTP_SERVICE_UNAVAILABLE:
  case error_code::HTTP_PROTOCOL_ERROR:
  case error_code::TOO_SLOW_DOWNLOAD_SPEED:
  case error_code::NETWORK_PROBLEM:
    return true;
  default:
    return false;
  }
}

bool isNumericAddressFamily(const std::string& addr, int family)
{
  union {
    in_addr ipv4;
    in6_addr ipv6;
  } buf;
  if (family == AF_INET) {
    return inetPton(family, addr.c_str(), &buf.ipv4) == 0;
  }
  return inetPton(family, addr.c_str(), &buf.ipv6) == 0;
}

int getNumericAddressFamily(const std::string& addr)
{
  if (isNumericAddressFamily(addr, AF_INET)) {
    return AF_INET;
  }
  if (isNumericAddressFamily(addr, AF_INET6)) {
    return AF_INET6;
  }
  return 0;
}

bool isSelectableAddressFamily(int family)
{
  return family == AF_INET || family == AF_INET6;
}

bool hasNumericAddressFamily(const std::vector<std::string>& addrs, int family)
{
  return std::find_if(std::begin(addrs), std::end(addrs),
                      [family](const std::string& addr) {
                        return getNumericAddressFamily(addr) == family;
                      }) != std::end(addrs);
}

int getFirstSelectableAddressFamily(const std::vector<std::string>& addrs)
{
  for (const auto& addr : addrs) {
    auto family = getNumericAddressFamily(addr);
    if (isSelectableAddressFamily(family)) {
      return family;
    }
  }
  return 0;
}

bool hasIPv6Address(const std::vector<std::string>& addrs,
                    bool (*pred)(const std::string&))
{
  return std::find_if(std::begin(addrs), std::end(addrs),
                      [pred](const std::string& addr) {
                        return getNumericAddressFamily(addr) == AF_INET6 &&
                               pred(addr);
                      }) != std::end(addrs);
}

bool shouldPreferIPv4OverScopedIPv6(const std::vector<std::string>& addrs)
{
  return hasNumericAddressFamily(addrs, AF_INET) &&
         hasNumericAddressFamily(addrs, AF_INET6) &&
         !hasIPv6Address(addrs, isIPv6GlobalUnicastAddress);
}

std::vector<std::string>::const_iterator
findPreferredAddressFamily(std::vector<std::string>::const_iterator first,
                           std::vector<std::string>::const_iterator last,
                           int family)
{
  if (family == AF_INET6) {
    auto globalIPv6 = std::find_if(
        first, last, [](const std::string& addr) {
          return getNumericAddressFamily(addr) == AF_INET6 &&
                 isIPv6GlobalUnicastAddress(addr);
        });
    if (globalIPv6 != last) {
      return globalIPv6;
    }
  }
  return std::find_if(first, last, [family](const std::string& addr) {
    return getNumericAddressFamily(addr) == family;
  });
}

int getOppositeAddressFamily(int family)
{
  if (family == AF_INET) {
    return AF_INET6;
  }
  if (family == AF_INET6) {
    return AF_INET;
  }
  return 0;
}

#ifdef ENABLE_ASYNC_DNS
struct AsyncServiceBindingDiscoveryResolver {
  using ResolveFunc = std::function<void(const std::string&, uint16_t)>;
  using RecordsFunc =
      std::function<const std::vector<dns::ServiceBindingRecord>&()>;

  AsyncServiceBindingDiscoveryResolver(std::shared_ptr<AsyncResolver> resolver,
                                       ResolveFunc resolve,
                                       RecordsFunc getRecords)
      : resolver(std::move(resolver)),
        resolve(std::move(resolve)),
        getRecords(std::move(getRecords)),
        checked(false),
        finished(false)
  {
  }

  std::shared_ptr<AsyncResolver> resolver;
  ResolveFunc resolve;
  RecordsFunc getRecords;
  bool checked;
  bool finished;
};

class AsyncDnsCacheCommand : public Command {
private:
  struct ResolverEntry {
    explicit ResolverEntry(std::shared_ptr<AsyncResolver> resolver)
        : resolver(std::move(resolver)), checked(false)
    {
    }

    std::shared_ptr<AsyncResolver> resolver;
    bool checked;
  };

  std::string hostname_;
  uint16_t port_;
  a2_gid_t gid_;
  std::vector<ResolverEntry> resolvers_;
  DownloadEngine* e_;
  Timer checkPoint_;
  std::chrono::seconds timeout_;

  void addCommandSelf() { e_->addCommand(std::unique_ptr<Command>(this)); }

  void deleteNameResolverCheck(ResolverEntry& entry)
  {
    if (entry.checked) {
      e_->deleteNameResolverCheck(entry.resolver, this);
      entry.checked = false;
    }
  }

  void deleteNameResolverChecks()
  {
    for (auto& entry : resolvers_) {
      if (entry.resolver) {
        deleteNameResolverCheck(entry);
      }
    }
  }

  void maybeCreateNextCommand()
  {
    auto group = e_->getRequestGroupMan()->findGroup(gid_);
    if (!group || group->getState() != RequestGroup::STATE_ACTIVE ||
        group->isHaltRequested() || group->isPauseRequested() ||
        group->downloadFinished()) {
      return;
    }

    std::vector<std::unique_ptr<Command>> commands;
    group->createNextCommand(commands, e_);
    if (!commands.empty()) {
      A2_LOG_NETWORK(fmt("DNS: background cache fill woke download GID#%s",
                         GroupId::toHex(gid_).c_str()));
      e_->addCommand(std::move(commands));
    }
  }

public:
  AsyncDnsCacheCommand(
      cuid_t cuid, std::string hostname, uint16_t port, a2_gid_t gid,
      std::vector<std::shared_ptr<AsyncResolver>> pendingResolvers,
      DownloadEngine* e, std::chrono::seconds timeout)
      : Command(cuid),
        hostname_(std::move(hostname)),
        port_(port),
        gid_(gid),
        e_(e),
        timeout_(std::move(timeout))
  {
    for (auto& resolver : pendingResolvers) {
      resolvers_.push_back(ResolverEntry(std::move(resolver)));
    }
  }

  ~AsyncDnsCacheCommand() { deleteNameResolverChecks(); }

  void start()
  {
    for (auto& entry : resolvers_) {
      if (entry.resolver && entry.resolver->usable()) {
        entry.checked = e_->addNameResolverCheck(entry.resolver, this);
      }
    }
    setStatusRealtime();
    e_->setNoWait(true);
  }

  bool execute() CXX11_OVERRIDE
  {
    if (e_->isHaltRequested() ||
        e_->getRequestGroupMan()->downloadFinished()) {
      return true;
    }

    if (checkPoint_.difference(global::wallclock()) >= timeout_) {
      A2_LOG_NETWORK(
          fmt("DNS: background cache fill timed out for %s", hostname_.c_str()));
      return true;
    }

    bool querying = false;
    bool addressCached = false;
    for (auto& entry : resolvers_) {
      if (!entry.resolver) {
        continue;
      }

      switch (entry.resolver->getStatus()) {
      case AsyncResolver::STATUS_SUCCESS:
        for (const auto& addr : entry.resolver->getResolvedAddresses()) {
          if (e_->cacheIPAddress(hostname_, addr, port_)) {
            addressCached = true;
            A2_LOG_NETWORK(
                fmt("DNS: background cache fill %s -> %s", hostname_.c_str(),
                    addr.c_str()));
          }
        }
        deleteNameResolverCheck(entry);
        entry.resolver.reset();
        break;
      case AsyncResolver::STATUS_ERROR:
        A2_LOG_NETWORK(
            fmt("DNS: background query failed for %s: %s", hostname_.c_str(),
                entry.resolver->getError().c_str()));
        deleteNameResolverCheck(entry);
        entry.resolver.reset();
        break;
      default:
        querying = true;
        break;
      }
    }

    if (addressCached) {
      maybeCreateNextCommand();
    }

    if (!querying) {
      return true;
    }

    setStatusRealtime();
    addCommandSelf();
    return false;
  }
};

class AsyncServiceBindingDiscoveryCommand : public Command {
private:
  std::string hostname_;
  uint16_t port_;
  a2_gid_t gid_;
  std::vector<AsyncServiceBindingDiscoveryResolver> resolvers_;
  DownloadEngine* e_;
  bool emptySuccess_;
  Timer checkPoint_;
  std::chrono::seconds timeout_;

  void addCommandSelf() { e_->addCommand(std::unique_ptr<Command>(this)); }

  void deleteNameResolverCheck(AsyncServiceBindingDiscoveryResolver& entry)
  {
    if (entry.checked) {
      e_->deleteNameResolverCheck(entry.resolver, this);
      entry.checked = false;
    }
  }

  void deleteNameResolverChecks()
  {
    for (auto& entry : resolvers_) {
      deleteNameResolverCheck(entry);
    }
  }

  static uint32_t getCacheTtl(
      const std::vector<dns::ServiceBindingRecord>& records)
  {
    if (records.empty()) {
      return 60;
    }

    uint32_t ttl = std::numeric_limits<uint32_t>::max();
    for (const auto& record : records) {
      ttl = std::min(ttl, record.ttl);
    }
    return ttl == std::numeric_limits<uint32_t>::max() ? 0 : ttl;
  }

  void finish()
  {
    deleteNameResolverChecks();
    e_->finishHttpsServiceBindingResolving(hostname_, port_);
  }

  void cacheRecords(const std::vector<dns::ServiceBindingRecord>& records)
  {
    auto ttl = getCacheTtl(records);
    if (ttl > 0) {
      e_->cacheHttpsServiceBindingRecords(hostname_, port_, records, ttl);
    }
    A2_LOG_NETWORK(
        fmt("DNS: HTTPS RR discovery for %s:%u cached %lu record(s) "
            "with ttl=%u for GID#%s",
            hostname_.c_str(), static_cast<unsigned int>(port_),
            static_cast<unsigned long>(records.size()),
            static_cast<unsigned int>(ttl), GroupId::toHex(gid_).c_str()));
  }

public:
  AsyncServiceBindingDiscoveryCommand(
      cuid_t cuid, std::string hostname, uint16_t port, a2_gid_t gid,
      std::vector<AsyncServiceBindingDiscoveryResolver> resolvers,
      DownloadEngine* e,
      std::chrono::seconds timeout)
      : Command(cuid),
        hostname_(std::move(hostname)),
        port_(port),
        gid_(gid),
        resolvers_(std::move(resolvers)),
        e_(e),
        emptySuccess_(false),
        timeout_(std::move(timeout))
  {
  }

  ~AsyncServiceBindingDiscoveryCommand() { deleteNameResolverChecks(); }

  void start()
  {
    for (auto& entry : resolvers_) {
      entry.resolve(hostname_, port_);
      if (entry.resolver->usable()) {
        entry.checked = e_->addNameResolverCheck(entry.resolver, this);
      }
    }
    setStatusRealtime();
    e_->setNoWait(true);
  }

  bool execute() CXX11_OVERRIDE
  {
    if (e_->isHaltRequested() ||
        e_->getRequestGroupMan()->downloadFinished()) {
      finish();
      return true;
    }

    if (checkPoint_.difference(global::wallclock()) >= timeout_) {
      A2_LOG_NETWORK(fmt("DNS: HTTPS RR discovery timed out for %s",
                         hostname_.c_str()));
      finish();
      return true;
    }

    bool querying = false;
    for (auto& entry : resolvers_) {
      if (entry.finished) {
        continue;
      }

      switch (entry.resolver->getStatus()) {
      case AsyncResolver::STATUS_SUCCESS: {
        deleteNameResolverCheck(entry);
        entry.finished = true;
        const auto& records = entry.getRecords();
        if (!records.empty()) {
          cacheRecords(records);
          finish();
          return true;
        }
        emptySuccess_ = true;
        break;
      }
      case AsyncResolver::STATUS_ERROR:
        A2_LOG_NETWORK(fmt("DNS: HTTPS RR discovery failed for %s:%u: %s",
                           hostname_.c_str(), static_cast<unsigned int>(port_),
                           entry.resolver->getError().c_str()));
        deleteNameResolverCheck(entry);
        entry.finished = true;
        break;
      default:
        querying = true;
        if (entry.resolver->usable() && !entry.checked) {
          entry.checked = e_->addNameResolverCheck(entry.resolver, this);
        }
        break;
      }
    }

    if (!querying) {
      if (emptySuccess_) {
        std::vector<dns::ServiceBindingRecord> emptyRecords;
        cacheRecords(emptyRecords);
      }
      finish();
      return true;
    }

    setStatusRealtime();
    addCommandSelf();
    return false;
  }
};

void appendServiceBindingResolver(
    std::vector<AsyncServiceBindingDiscoveryResolver>& resolvers,
    std::shared_ptr<AsyncServiceBindingResolver> resolver)
{
  resolvers.emplace_back(
      resolver,
      [resolver](const std::string& hostname, uint16_t port) {
        resolver->resolve(hostname, port);
      },
      [resolver]() -> const std::vector<dns::ServiceBindingRecord>& {
        return resolver->getServiceBindingRecords();
      });
}

#ifdef ENABLE_SSL
int getServiceBindingBootstrapFamily(const Option* option)
{
  auto ipv4 = net::getIPv4AddrConfigured();
  auto ipv6 =
      net::getIPv6AddrConfigured() && !option->getAsBool(PREF_DISABLE_IPV6);
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

bool isDohHttp2Enabled(const Option* option)
{
  return option->getAsBool(PREF_ENABLE_HTTP2) &&
         !option->getAsBool(PREF_ENABLE_HTTP_PIPELINING);
}

void appendServiceBindingResolver(
    std::vector<AsyncServiceBindingDiscoveryResolver>& resolvers,
    std::shared_ptr<AsyncDotNameResolver> resolver)
{
  resolvers.emplace_back(
      resolver,
      [resolver](const std::string& hostname, uint16_t port) {
        resolver->resolveHttpsServiceBinding(hostname, port);
      },
      [resolver]() -> const std::vector<dns::ServiceBindingRecord>& {
        return resolver->getServiceBindingRecords();
      });
}

void appendServiceBindingResolver(
    std::vector<AsyncServiceBindingDiscoveryResolver>& resolvers,
    std::shared_ptr<AsyncDohNameResolver> resolver)
{
  resolvers.emplace_back(
      resolver,
      [resolver](const std::string& hostname, uint16_t port) {
        resolver->resolveHttpsServiceBinding(hostname, port);
      },
      [resolver]() -> const std::vector<dns::ServiceBindingRecord>& {
        return resolver->getServiceBindingRecords();
      });
}
#endif // ENABLE_SSL

std::vector<AsyncServiceBindingDiscoveryResolver>
createServiceBindingDiscoveryResolvers(const Option* option)
{
  std::vector<AsyncServiceBindingDiscoveryResolver> resolvers;
  const auto& mode = option->get(PREF_ASYNC_DNS_MODE);
  const auto& servers = option->get(PREF_ASYNC_DNS_SERVER);
  if (mode == V_CARES) {
    appendServiceBindingResolver(
        resolvers, std::make_shared<AsyncServiceBindingResolver>(servers));
    return resolvers;
  }

#ifdef ENABLE_SSL
  auto bootstrapFamily = getServiceBindingBootstrapFamily(option);
  if (mode == V_DOT) {
    auto dotServers = parseAsyncDnsDotServerConfigList(servers);
    validateAsyncDnsDotServerConfig(dotServers);
    appendServiceBindingResolver(
        resolvers, std::make_shared<AsyncDotNameResolver>(
                       AF_UNSPEC, std::move(dotServers),
                       AsyncDotTransportFactory(),
                       AsyncDotBootstrapResolverFactory(), bootstrapFamily));
  }
  else if (mode == V_DOH) {
    auto dohServers = parseAsyncDnsDohServerConfigList(servers);
    validateAsyncDnsDohServerConfig(dohServers);
    appendServiceBindingResolver(
        resolvers, std::make_shared<AsyncDohNameResolver>(
                       AF_UNSPEC, std::move(dohServers),
                       AsyncDohTransportFactory(), isDohHttp2Enabled(option),
                       AsyncDohBootstrapResolverFactory(), bootstrapFamily));
  }
  else if (mode == V_MULTI) {
    auto config = parseAsyncDnsMultiServerConfigList(servers);
    auto hasSecureServers =
        !config.dotServers.empty() || !config.dohServers.empty();
    if (!config.udpServers.empty()) {
      appendServiceBindingResolver(
          resolvers,
          std::make_shared<AsyncServiceBindingResolver>(config.udpServers));
    }
    if (!config.tcpServers.empty()) {
      appendServiceBindingResolver(
          resolvers,
          std::make_shared<AsyncServiceBindingResolver>(config.tcpServers,
                                                        true));
    }
    if (!hasSecureServers && config.udpServers.empty() &&
        config.tcpServers.empty()) {
      appendServiceBindingResolver(
          resolvers, std::make_shared<AsyncServiceBindingResolver>());
    }
    if (!config.dotServers.empty()) {
      appendServiceBindingResolver(
          resolvers, std::make_shared<AsyncDotNameResolver>(
                         AF_UNSPEC, std::move(config.dotServers),
                         AsyncDotTransportFactory(),
                         AsyncDotBootstrapResolverFactory(), bootstrapFamily));
    }
    if (!config.dohServers.empty()) {
      appendServiceBindingResolver(
          resolvers, std::make_shared<AsyncDohNameResolver>(
                         AF_UNSPEC, std::move(config.dohServers),
                         AsyncDohTransportFactory(),
                         isDohHttp2Enabled(option),
                         AsyncDohBootstrapResolverFactory(), bootstrapFamily));
    }
  }
#endif // ENABLE_SSL

  return resolvers;
}

bool addAsyncDnsCacheCommand(
    DownloadEngine* e, const std::string& hostname, uint16_t port,
    a2_gid_t gid,
    std::vector<std::shared_ptr<AsyncResolver>> pendingResolvers)
{
  if (pendingResolvers.empty()) {
    return false;
  }

  auto command = make_unique<AsyncDnsCacheCommand>(
      e->newCUID(), hostname, port, gid, std::move(pendingResolvers), e,
      std::chrono::seconds(e->getOption()->getAsInt(PREF_DNS_TIMEOUT)));
  command->start();
  e->addCommand(std::move(command));
  return true;
}

void maybeStartHttpsServiceBindingDiscovery(DownloadEngine* e,
                                            const std::string& hostname,
                                            uint16_t port, a2_gid_t gid)
{
  if (!e || !e->getOption() ||
      !e->getOption()->getAsBool(PREF_ASYNC_DNS) ||
      e->findCachedHttpsServiceBindingRecords(hostname, port) ||
      e->isHttpsServiceBindingResolving(hostname, port)) {
    return;
  }

  auto resolvers = createServiceBindingDiscoveryResolvers(e->getOption());
  if (resolvers.empty()) {
    return;
  }

  if (!e->markHttpsServiceBindingResolving(hostname, port)) {
    return;
  }

  auto command = make_unique<AsyncServiceBindingDiscoveryCommand>(
      e->newCUID(), hostname, port, gid, std::move(resolvers), e,
      std::chrono::seconds(e->getOption()->getAsInt(PREF_DNS_TIMEOUT)));
  command->start();
  e->addCommand(std::move(command));
}

bool continueAsyncDnsCacheFill(DownloadEngine* e, const std::string& hostname,
                               uint16_t port,
                               AsyncNameResolverMan* asyncNameResolverMan,
                               RequestGroup* requestGroup, Command* command)
{
  if (!asyncNameResolverMan->started()) {
    return false;
  }

  auto pendingResolvers = asyncNameResolverMan->detachPendingResolvers(e,
                                                                        command);
  asyncNameResolverMan->reset(e, command);
  return addAsyncDnsCacheCommand(e, hostname, port, requestGroup->getGID(),
                                 std::move(pendingResolvers));
}

void preserveAsyncDnsCacheFill(DownloadEngine* e, const std::string& hostname,
                               uint16_t port,
                               AsyncNameResolverMan* asyncNameResolverMan,
                               RequestGroup* requestGroup, Command* command)
{
  if (!asyncNameResolverMan->started()) {
    return;
  }

  std::vector<std::string> resolvedAddrs;
  asyncNameResolverMan->getResolvedAddress(resolvedAddrs);
  for (const auto& addr : resolvedAddrs) {
    e->cacheIPAddress(hostname, addr, port);
  }

  (void)continueAsyncDnsCacheFill(e, hostname, port, asyncNameResolverMan,
                                  requestGroup, command);
}
#endif // ENABLE_ASYNC_DNS
} // namespace

AbstractCommand::AbstractCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    DownloadEngine* e, const std::shared_ptr<SocketCore>& s,
    const std::shared_ptr<SocketRecvBuffer>& socketRecvBuffer,
    bool incNumConnection)
    : Command(cuid),
      req_(req),
      fileEntry_(fileEntry),
      socket_(s),
      socketRecvBuffer_(socketRecvBuffer),
#ifdef ENABLE_ASYNC_DNS
      asyncNameResolverMan_(make_unique<AsyncNameResolverMan>()),
#endif // ENABLE_ASYNC_DNS
      requestGroup_(requestGroup),
      e_(e),
      checkPoint_(global::wallclock()),
      serverStatTimer_(global::wallclock()),
      timeout_(requestGroup->getTimeout()),
      checkSocketIsReadable_(false),
      checkSocketIsWritable_(false),
      incNumConnection_(incNumConnection)
{
  if (socket_ && socket_->isOpen()) {
    setReadCheckSocket(socket_);
  }
  if (incNumConnection_) {
    requestGroup->increaseStreamConnection();
  }
  requestGroup_->increaseStreamCommand();
  requestGroup_->increaseNumCommand();
#ifdef ENABLE_ASYNC_DNS
  configureAsyncNameResolverMan(asyncNameResolverMan_.get(), e_->getOption());
#endif // ENABLE_ASYNC_DNS
}

AbstractCommand::~AbstractCommand()
{
  disableReadCheckSocket();
  disableWriteCheckSocket();
#ifdef ENABLE_ASYNC_DNS
  asyncNameResolverMan_->disableNameResolverCheck(e_, this);
#endif // ENABLE_ASYNC_DNS
  requestGroup_->decreaseNumCommand();
  requestGroup_->decreaseStreamCommand();
  if (incNumConnection_) {
    requestGroup_->decreaseStreamConnection();
  }
}

void AbstractCommand::useFasterRequest(
    const std::shared_ptr<Request>& fasterRequest)
{
  A2_LOG_INFO(fmt("CUID#%" PRId64 " - Use faster Request hostname=%s, port=%u",
                  getCuid(), fasterRequest->getHost().c_str(),
                  fasterRequest->getPort()));
  // Cancel current Request object and use faster one.
  fileEntry_->removeRequest(req_);
  e_->setNoWait(true);
  e_->addCommand(
      InitiateConnectionCommandFactory::createInitiateConnectionCommand(
          getCuid(), fasterRequest, fileEntry_, requestGroup_, e_));
}

bool AbstractCommand::shouldProcess() const
{
  if (checkSocketIsReadable_) {
    if (readEventEnabled()) {
      return true;
    }

    if (socketRecvBuffer_ && !socketRecvBuffer_->bufferEmpty()) {
      return true;
    }

    if (socket_ && socket_->getRecvBufferedLength()) {
      return true;
    }
  }

  if (checkSocketIsWritable_ && writeEventEnabled()) {
    return true;
  }

#ifdef ENABLE_ASYNC_DNS
  const auto resolverChecked = asyncNameResolverMan_->resolverChecked();
  if (resolverChecked && asyncNameResolverMan_->getStatus() != 0) {
    return true;
  }

  if (!checkSocketIsReadable_ && !checkSocketIsWritable_ && !resolverChecked) {
    return true;
  }
#else  // ENABLE_ASYNC_DNS
  if (!checkSocketIsReadable_ && !checkSocketIsWritable_) {
    return true;
  }
#endif // ENABLE_ASYNC_DNS

  return noCheck();
}

bool AbstractCommand::execute()
{
  A2_LOG_DEBUG(fmt("CUID#%" PRId64
                   " - socket: read:%d, write:%d, hup:%d, err:%d",
                   getCuid(), readEventEnabled(), writeEventEnabled(),
                   hupEventEnabled(), errorEventEnabled()));
  try {
    if (requestGroup_->downloadFinished() || requestGroup_->isHaltRequested()) {
      return true;
    }

    if (req_ && req_->removalRequested()) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64
                       " - Discard original URI=%s because it is"
                       " requested.",
                       getCuid(), req_->getUri().c_str()));
      return prepareForRetry(0);
    }

    auto sm = getSegmentMan();

    if (getPieceStorage()) {
      refreshSegments();

      if (req_ && segments_.empty()) {
        // This command previously has assigned segments, but it is
        // canceled. So discard current request chain.  Plus, if no
        // segment is available when http pipelining is used.
        A2_LOG_DEBUG(fmt("CUID#%" PRId64
                         " - It seems previously assigned segments"
                         " are canceled. Restart.",
                         getCuid()));
        // Request::isPipeliningEnabled() == true means aria2
        // accessed the remote server and discovered that the server
        // supports pipelining.
        if (req_ && req_->isPipeliningEnabled()) {
          e_->poolSocket(req_, createProxyRequest(), socket_);
        }
        return prepareForRetry(0);
      }

      // TODO it is not needed to check other PeerStats every time.
      // Find faster Request when no segment split is allowed.
      if (req_ && fileEntry_->countPooledRequest() > 0 &&
          requestGroup_->getPendingLength() < calculateMinSplitSize() * 2) {
        auto fasterRequest = fileEntry_->findFasterRequest(req_);
        if (fasterRequest) {
          useFasterRequest(fasterRequest);
          return true;
        }
      }
      // Don't use this feature if PREF_MAX_{OVERALL_}DOWNLOAD_LIMIT
      // is used or total length is unknown.
      if (req_ && fileEntry_->getLength() > 0 &&
          e_->getRequestGroupMan()->getMaxOverallDownloadSpeedLimit() == 0 &&
          requestGroup_->getMaxDownloadSpeedLimit() == 0 &&
          serverStatTimer_.difference(global::wallclock()) >= 10_s) {
        serverStatTimer_ = global::wallclock();
        std::vector<std::pair<size_t, std::string>> usedHosts;
        if (getOption()->getAsBool(PREF_SELECT_LEAST_USED_HOST)) {
          getDownloadEngine()->getRequestGroupMan()->getUsedHosts(usedHosts);
        }
        auto fasterRequest = fileEntry_->findFasterRequest(
            req_, usedHosts, e_->getRequestGroupMan()->getServerStatMan());
        if (fasterRequest) {
          useFasterRequest(fasterRequest);
          return true;
        }
      }
    }

    if (shouldProcess()) {
      checkPoint_ = global::wallclock();

      if (!getPieceStorage()) {
        return executeInternal();
      }

      if (!req_ || req_->getMaxPipelinedRequest() == 1 ||
          // Why the following condition is necessary? That's because
          // For single file download, SegmentMan::getSegment(cuid)
          // is more efficient.
          getDownloadContext()->getFileEntries().size() == 1) {
        size_t maxSegments = req_ ? req_->getMaxPipelinedRequest() : 1;
        size_t minSplitSize = calculateMinSplitSize();
        while (segments_.size() < maxSegments) {
          auto segment = sm->getSegment(getCuid(), minSplitSize);
          if (!segment) {
            break;
          }
          segments_.push_back(segment);
        }
        if (segments_.empty()) {
          // TODO socket could be pooled here if pipelining is
          // enabled...  Hmm, I don't think if pipelining is enabled
          // it does not go here.
          A2_LOG_INFO(fmt(MSG_NO_SEGMENT_AVAILABLE, getCuid()));
          // When all segments are ignored in SegmentMan, there are
          // no URIs available, so don't retry.
          if (sm->allSegmentsIgnored()) {
            A2_LOG_DEBUG("All segments are ignored.");
            // This will execute other idle Commands and let them
            // finish quickly.
            e_->setRefreshInterval(std::chrono::milliseconds(0));
            return true;
          }

          return prepareForRetry(1);
        }
      }
      else {
        // For multi-file downloads
        size_t minSplitSize = calculateMinSplitSize();
        size_t maxSegments = req_->getMaxPipelinedRequest();
        if (segments_.size() < maxSegments) {
          sm->getSegment(segments_, getCuid(), minSplitSize, fileEntry_,
                         maxSegments);
        }
        if (segments_.empty()) {
          return prepareForRetry(0);
        }
      }

      return executeInternal();
    }

    if (errorEventEnabled()) {
      // older kernel may report "connection refused" here.
      auto ss = e_->getRequestGroupMan()->getOrCreateServerStat(
          req_->getHost(), req_->getProtocol());
      ss->setError();

      A2_LOG_NETWORK(fmt("CUID#%" PRId64 " - Network problem while talking to %s: "
                         "%s, retrying",
                         getCuid(), req_->getUri().c_str(),
                         socket_->getSocketError().c_str()));
      throw DL_RETRY_EX(
          fmt(MSG_NETWORK_PROBLEM, socket_->getSocketError().c_str()));
    }

    if (checkPoint_.difference(global::wallclock()) >= timeout_) {
      // timeout triggers ServerStat error state.
      auto ss = e_->getRequestGroupMan()->getOrCreateServerStat(
          req_->getHost(), req_->getProtocol());
      ss->setError();
      // When DNS query was timeout, req_->getConnectedAddr() is
      // empty.
      const auto& connectedAddr = req_->getConnectedAddr();
      const auto& connectedHostname = req_->getConnectedHostname();
      const auto connectedPort = req_->getConnectedPort();
      const bool dnsTimeout = connectedAddr.empty();
      if (!connectedAddr.empty()) {
        // Purging IP address cache to renew IP address.
        A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Marking IP address %s as bad",
                         getCuid(), connectedAddr.c_str()));
        A2_LOG_NETWORK(
            fmt("CUID#%" PRId64 " - Marking IP address %s as bad (timeout)",
                getCuid(), connectedAddr.c_str()));
        e_->markBadIPAddress(connectedHostname, connectedAddr, connectedPort);
      }
      if (!connectedHostname.empty() &&
          e_->findCachedIPAddress(connectedHostname, connectedPort).empty()) {
        auto mappedAddrs =
            getMappedAddresses(connectedHostname, getOption().get());
        if (!mappedAddrs.empty()) {
          throw DL_ABORT_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED,
                                "No host mapping address left to try"));
        }
        A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - All IP addresses were marked bad."
                         " Removing Entry.",
                         getCuid()));
        A2_LOG_NETWORK(
            fmt("CUID#%" PRId64 " - All IP addresses for %s were marked bad",
                getCuid(), connectedHostname.c_str()));
        e_->removeCachedIPAddress(connectedHostname, connectedPort);
      }
#ifdef ENABLE_ASYNC_DNS
      if (dnsTimeout && asyncNameResolverMan_->started()) {
        auto status = asyncNameResolverMan_->getQueryStatus();
        if (!status.empty()) {
          A2_LOG_NETWORK(
              fmt("CUID#%" PRId64 " - DNS lookup for %s timed out after %ld "
                  "seconds (%s), retrying",
                  getCuid(), req_->getHost().c_str(),
                  static_cast<long>(timeout_.count()), status.c_str()));
        }
        else {
          A2_LOG_NETWORK(
              fmt("CUID#%" PRId64 " - DNS lookup for %s timed out after %ld "
                  "seconds, retrying",
                  getCuid(), req_->getHost().c_str(),
                  static_cast<long>(timeout_.count())));
        }
      }
      else
#endif // ENABLE_ASYNC_DNS
      if (dnsTimeout) {
        A2_LOG_NETWORK(
            fmt("CUID#%" PRId64 " - DNS lookup for %s timed out after %ld "
                "seconds, retrying",
                getCuid(), req_->getHost().c_str(),
                static_cast<long>(timeout_.count())));
      }
      else {
        A2_LOG_NETWORK(
            fmt("CUID#%" PRId64 " - Connection to %s timed out, retrying",
                getCuid(), req_->getUri().c_str()));
      }
      throw DL_RETRY_EX2(EX_TIME_OUT, error_code::TIME_OUT);
    }

    addCommandSelf();
    return false;
  }
  catch (DlAbortEx& err) {
    requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
    if (req_) {
      A2_LOG_ERROR_EX(
          fmt(MSG_DOWNLOAD_ABORTED, getCuid(), req_->getUri().c_str()),
          DL_ABORT_EX2(fmt("URI=%s", req_->getCurrentUri().c_str()), err));
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
      if (err.getErrorCode() == error_code::CANNOT_RESUME) {
        requestGroup_->increaseResumeFailureCount();
      }
    }
    else {
      A2_LOG_DEBUG_EX(EX_EXCEPTION_CAUGHT, err);
    }
    onAbort();
    tryReserved();
    return true;
  }
  catch (DlRetryEx& err) {
    assert(req_);
    A2_LOG_INFO_EX(
        fmt(MSG_RESTARTING_DOWNLOAD, getCuid(), req_->getUri().c_str()),
        DL_RETRY_EX2(fmt("URI=%s", req_->getCurrentUri().c_str()), err));
    req_->addTryCount();
    req_->resetRedirectCount();
    req_->resetUri();

    const int maxTries = getOption()->getAsInt(PREF_MAX_TRIES);
    bool isAbort = maxTries != 0 && req_->getTryCount() >= maxTries;
    if (isAbort) {
      if (err.getErrorCode() == error_code::TIME_OUT ||
          err.getErrorCode() == error_code::RESOURCE_NOT_FOUND ||
          shouldWaitBeforeRetry(err.getErrorCode())) {
        A2_LOG_NETWORK(
            fmt("CUID#%" PRId64 " - Retries exhausted (max=%d) for %s",
                getCuid(), maxTries, req_->getUri().c_str()));
      }
      A2_LOG_INFO(fmt(MSG_MAX_TRY, getCuid(), req_->getTryCount()));
      A2_LOG_ERROR_EX(
          fmt(MSG_DOWNLOAD_ABORTED, getCuid(), req_->getUri().c_str()), err);
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
      requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
      if (err.getErrorCode() == error_code::CANNOT_RESUME) {
        requestGroup_->increaseResumeFailureCount();
      }
      onAbort();
      tryReserved();
      return true;
    }

    if (shouldWaitBeforeRetry(err.getErrorCode())) {
      A2_LOG_NETWORK(fmt(
          "CUID#%" PRId64
          " - Waiting %d seconds before retry",
          getCuid(), getOption()->getAsInt(PREF_RETRY_WAIT)));
      Timer wakeTime(global::wallclock());
      wakeTime.advance(
          std::chrono::seconds(getOption()->getAsInt(PREF_RETRY_WAIT)));
      req_->setWakeTime(wakeTime);
    }

    return prepareForRetry(0);
  }
  catch (DownloadFailureException& err) {
    requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
    if (req_) {
      A2_LOG_ERROR_EX(
          fmt(MSG_DOWNLOAD_ABORTED, getCuid(), req_->getUri().c_str()),
          DL_ABORT_EX2(fmt("URI=%s", req_->getCurrentUri().c_str()), err));
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
    }
    else {
      A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, err);
    }
    requestGroup_->setHaltRequested(true);
    getDownloadEngine()->setRefreshInterval(std::chrono::milliseconds(0));
    return true;
  }
}

void AbstractCommand::tryReserved()
{
  if (getDownloadContext()->getFileEntries().size() == 1) {
    const auto& entry = getDownloadContext()->getFirstFileEntry();
    // Don't create new command if currently file length is unknown
    // and there are no URI left. Because file length is unknown, we
    // can assume that there are no in-flight request object.
    if (entry->getLength() == 0 && entry->getRemainingUris().empty()) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Not trying next request."
                       " No reserved/pooled request is remaining and"
                       " total length is still unknown.",
                       getCuid()));
      return;
    }
  }
  A2_LOG_DEBUG(
      fmt("CUID#%" PRId64 " - Trying reserved/pooled request.", getCuid()));
  std::vector<std::unique_ptr<Command>> commands;
  requestGroup_->createNextCommand(commands, e_, 1);
  e_->setNoWait(true);
  e_->addCommand(std::move(commands));
}

bool AbstractCommand::prepareForRetry(time_t wait)
{
  if (getPieceStorage()) {
    getSegmentMan()->cancelSegment(getCuid());
  }
  if (req_) {
    // Reset persistentConnection and maxPipelinedRequest to handle
    // the situation where remote server returns Connection: close
    // after several pipelined requests.
    req_->supportsPersistentConnection(true);
    req_->setMaxPipelinedRequest(1);

    fileEntry_->poolRequest(req_);
    A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Pooling request URI=%s", getCuid(),
                     req_->getUri().c_str()));
    if (getSegmentMan()) {
      getSegmentMan()->recognizeSegmentFor(fileEntry_);
    }
  }

  auto command =
      make_unique<CreateRequestCommand>(getCuid(), requestGroup_, e_);
  if (wait == 0) {
    e_->setNoWait(true);
  }
  else {
    // We don't use wait so that Command can be executed by
    // DownloadEngine::setRefreshInterval(std::chrono::milliseconds(0)).
    command->setStatus(Command::STATUS_INACTIVE);
  }
  e_->addCommand(std::move(command));
  return true;
}

void AbstractCommand::onAbort()
{
  if (req_) {
    fileEntry_->removeIdenticalURI(req_->getUri());
    fileEntry_->removeRequest(req_);
  }

  A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Aborting download", getCuid()));
  if (!getPieceStorage()) {
    return;
  }

  getSegmentMan()->cancelSegment(getCuid());
  // Don't do following process if BitTorrent is involved or files
  // in DownloadContext is more than 1. The latter condition is
  // limitation of current implementation.
  if (getOption()->getAsBool(PREF_ALWAYS_RESUME) || !fileEntry_ ||
      getDownloadContext()->getNetStat().getSessionDownloadLength() != 0 ||
      requestGroup_->p2pInvolved() ||
      getDownloadContext()->getFileEntries().size() != 1) {
    return;
  }

  const int maxTries = getOption()->getAsInt(PREF_MAX_RESUME_FAILURE_TRIES);
  if (!(maxTries > 0 && requestGroup_->getResumeFailureCount() >= maxTries) &&
      !fileEntry_->emptyRequestUri()) {
    return;
  }
  // Local file exists, but given servers(or at least contacted
  // ones) doesn't support resume. Let's restart download from
  // scratch.
  A2_LOG_NOTICE(fmt(_("CUID#%" PRId64 " - Failed to resume download."
                      " Download from scratch."),
                    getCuid()));
  A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Gathering URIs that has CANNOT_RESUME"
                   " error",
                   getCuid()));
  // Set PREF_ALWAYS_RESUME to A2_V_TRUE to avoid repeating this
  // process.
  getOption()->put(PREF_ALWAYS_RESUME, A2_V_TRUE);
  std::deque<URIResult> res;
  fileEntry_->extractURIResult(res, error_code::CANNOT_RESUME);
  if (res.empty()) {
    return;
  }

  getSegmentMan()->cancelAllSegments();
  getSegmentMan()->eraseSegmentWrittenLengthMemo();
  getPieceStorage()->markPiecesDone(0);
  std::vector<std::string> uris;
  uris.reserve(res.size());
  std::transform(std::begin(res), std::end(res), std::back_inserter(uris),
                 std::mem_fn(&URIResult::getURI));
  A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - %lu URIs found.", getCuid(),
                   static_cast<unsigned long int>(uris.size())));
  fileEntry_->addUris(std::begin(uris), std::end(uris));
  getSegmentMan()->recognizeSegmentFor(fileEntry_);
}

void AbstractCommand::disableReadCheckSocket()
{
  if (!checkSocketIsReadable_) {
    return;
  }

  e_->deleteSocketForReadCheck(readCheckTarget_, this);
  checkSocketIsReadable_ = false;
  readCheckTarget_.reset();
}

void AbstractCommand::setReadCheckSocket(
    const std::shared_ptr<SocketCore>& socket)
{
  if (!socket->isOpen()) {
    disableReadCheckSocket();
    return;
  }

  if (checkSocketIsReadable_) {
    if (*readCheckTarget_ != *socket) {
      e_->deleteSocketForReadCheck(readCheckTarget_, this);
      e_->addSocketForReadCheck(socket, this);
      readCheckTarget_ = socket;
    }
    return;
  }

  e_->addSocketForReadCheck(socket, this);
  checkSocketIsReadable_ = true;
  readCheckTarget_ = socket;
}

void AbstractCommand::setReadCheckSocketIf(
    const std::shared_ptr<SocketCore>& socket, bool pred)
{
  if (pred) {
    setReadCheckSocket(socket);
    return;
  }

  disableReadCheckSocket();
}

void AbstractCommand::disableWriteCheckSocket()
{
  if (!checkSocketIsWritable_) {
    return;
  }
  e_->deleteSocketForWriteCheck(writeCheckTarget_, this);
  checkSocketIsWritable_ = false;
  writeCheckTarget_.reset();
}

void AbstractCommand::setWriteCheckSocket(
    const std::shared_ptr<SocketCore>& socket)
{
  if (!socket->isOpen()) {
    disableWriteCheckSocket();
    return;
  }

  if (checkSocketIsWritable_) {
    if (*writeCheckTarget_ != *socket) {
      e_->deleteSocketForWriteCheck(writeCheckTarget_, this);
      e_->addSocketForWriteCheck(socket, this);
      writeCheckTarget_ = socket;
    }
    return;
  }

  e_->addSocketForWriteCheck(socket, this);
  checkSocketIsWritable_ = true;
  writeCheckTarget_ = socket;
}

void AbstractCommand::setWriteCheckSocketIf(
    const std::shared_ptr<SocketCore>& socket, bool pred)
{
  if (pred) {
    setWriteCheckSocket(socket);
    return;
  }

  disableWriteCheckSocket();
}

void AbstractCommand::swapSocket(std::shared_ptr<SocketCore>& socket)
{
  disableReadCheckSocket();
  disableWriteCheckSocket();
  socket_.swap(socket);
}

namespace {
// Constructs proxy URI, merging username and password if they are
// defined.
std::string makeProxyUri(PrefPtr proxyPref, PrefPtr proxyUser,
                         PrefPtr proxyPasswd, const Option* option)
{
  uri::UriStruct us;
  if (!uri::parse(us, option->get(proxyPref))) {
    return "";
  }
  if (option->defined(proxyUser)) {
    us.username = option->get(proxyUser);
  }
  if (option->defined(proxyPasswd)) {
    us.password = option->get(proxyPasswd);
    us.hasPassword = true;
  }
  return uri::construct(us);
}
} // namespace

namespace {
// Returns proxy option value for the given protocol.
std::string getProxyOptionFor(PrefPtr proxyPref, PrefPtr proxyUser,
                              PrefPtr proxyPasswd, const Option* option)
{
  std::string uri = makeProxyUri(proxyPref, proxyUser, proxyPasswd, option);
  if (uri.empty()) {
    return makeProxyUri(PREF_ALL_PROXY, PREF_ALL_PROXY_USER,
                        PREF_ALL_PROXY_PASSWD, option);
  }

  return uri;
}
} // namespace

// Returns proxy URI for given protocol.  If no proxy URI is defined,
// then returns an empty string.
std::string getProxyUri(const std::string& protocol, const Option* option)
{
  if (protocol == "http") {
    return getProxyOptionFor(PREF_HTTP_PROXY, PREF_HTTP_PROXY_USER,
                             PREF_HTTP_PROXY_PASSWD, option);
  }

  if (protocol == "https") {
    return getProxyOptionFor(PREF_HTTPS_PROXY, PREF_HTTPS_PROXY_USER,
                             PREF_HTTPS_PROXY_PASSWD, option);
  }

  if (protocol == "ftp" || protocol == "sftp") {
    return getProxyOptionFor(PREF_FTP_PROXY, PREF_FTP_PROXY_USER,
                             PREF_FTP_PROXY_PASSWD, option);
  }

  return A2STR::NIL;
}

std::string selectIPAddress(const std::vector<std::string>& addrs,
                            cuid_t cuid, int preferredFamily)
{
  if (addrs.empty()) {
    return A2STR::NIL;
  }

  if (!hasNumericAddressFamily(addrs, AF_INET) ||
      !hasNumericAddressFamily(addrs, AF_INET6)) {
    return addrs.front();
  }

  auto family = 0;
  if (isSelectableAddressFamily(preferredFamily)) {
    family = preferredFamily;
  }
  else if (shouldPreferIPv4OverScopedIPv6(addrs)) {
    family = AF_INET;
  }
  else {
    family = cuid % 2 == 0 ? AF_INET : AF_INET6;
  }
  auto addr =
      findPreferredAddressFamily(std::begin(addrs), std::end(addrs), family);
  return addr == std::end(addrs) ? addrs.front() : *addr;
}

std::string selectIPAddress(const std::vector<std::string>& addrs,
                            cuid_t cuid)
{
  return selectIPAddress(addrs, cuid, 0);
}

void prioritizeIPAddress(std::vector<std::string>& addrs,
                          const std::string& ipaddr)
{
  auto i = std::find(std::begin(addrs), std::end(addrs), ipaddr);
  if (i == std::end(addrs) || i == std::begin(addrs)) {
    return;
  }
  auto selected = std::move(*i);
  addrs.erase(i);
  addrs.insert(std::begin(addrs), std::move(selected));
}

void prioritizeAndInterleaveIPAddress(std::vector<std::string>& addrs,
                                       const std::string& ipaddr)
{
  auto selectedFamily = getNumericAddressFamily(ipaddr);
  if (!isSelectableAddressFamily(selectedFamily) ||
      !hasNumericAddressFamily(addrs, AF_INET) ||
      !hasNumericAddressFamily(addrs, AF_INET6)) {
    prioritizeIPAddress(addrs, ipaddr);
    return;
  }

  std::vector<std::string> ipv4Addrs;
  std::vector<std::string> ipv6Addrs;
  std::vector<std::string> otherAddrs;
  bool foundSelected = false;
  for (const auto& addr : addrs) {
    if (!foundSelected && addr == ipaddr) {
      foundSelected = true;
      continue;
    }

    auto family = getNumericAddressFamily(addr);
    if (family == AF_INET) {
      ipv4Addrs.push_back(addr);
    }
    else if (family == AF_INET6) {
      ipv6Addrs.push_back(addr);
    }
    else {
      otherAddrs.push_back(addr);
    }
  }

  if (!foundSelected) {
    prioritizeIPAddress(addrs, ipaddr);
    return;
  }

  addrs.clear();
  addrs.push_back(ipaddr);

  size_t ipv4Index = 0;
  size_t ipv6Index = 0;
  auto nextFamily = getOppositeAddressFamily(selectedFamily);
  while (ipv4Index < ipv4Addrs.size() || ipv6Index < ipv6Addrs.size()) {
    if (nextFamily == AF_INET) {
      if (ipv4Index < ipv4Addrs.size()) {
        addrs.push_back(ipv4Addrs[ipv4Index++]);
      }
      nextFamily = AF_INET6;
    }
    else {
      if (ipv6Index < ipv6Addrs.size()) {
        addrs.push_back(ipv6Addrs[ipv6Index++]);
      }
      nextFamily = AF_INET;
    }
  }
  addrs.insert(std::end(addrs), std::begin(otherAddrs),
               std::end(otherAddrs));
}

namespace {
int getLeastUsedActiveAddressFamily(
    const std::shared_ptr<FileEntry>& fileEntry, const std::string& hostname,
    uint16_t port, const std::vector<std::string>& addrs)
{
  if (!fileEntry) {
    return 0;
  }

  if (!hasNumericAddressFamily(addrs, AF_INET) ||
      !hasNumericAddressFamily(addrs, AF_INET6)) {
    return 0;
  }

  size_t ipv4 = 0;
  size_t ipv6 = 0;
  for (const auto& request : fileEntry->getInFlightRequests()) {
    if (request->getConnectedHostname() != hostname ||
        request->getConnectedPort() != port) {
      continue;
    }
    const auto& connectedAddr = request->getConnectedAddr();
    auto family = getNumericAddressFamily(connectedAddr);
    if (family == AF_INET) {
      ++ipv4;
    }
    else if (family == AF_INET6) {
      ++ipv6;
    }
  }
  if (ipv4 == ipv6) {
    return 0;
  }
  return ipv4 < ipv6 ? AF_INET : AF_INET6;
}
} // namespace

std::string selectIPAddress(const std::vector<std::string>& addrs, cuid_t cuid,
                            const std::shared_ptr<FileEntry>& fileEntry,
                            const std::string& hostname, uint16_t port)
{
  auto preferIPv4 = shouldPreferIPv4OverScopedIPv6(addrs);
  auto family = preferIPv4 ? AF_INET : getLeastUsedActiveAddressFamily(
                                           fileEntry, hostname, port, addrs);
  if (!preferIPv4 && !isSelectableAddressFamily(family) && fileEntry &&
      hasNumericAddressFamily(addrs, AF_INET) &&
      hasNumericAddressFamily(addrs, AF_INET6)) {
    auto fallbackFamily = getFirstSelectableAddressFamily(addrs);
    family = fileEntry->getNextAddressFamily(hostname, port, fallbackFamily);
  }

  auto ipaddr = selectIPAddress(addrs, cuid, family);
  if (fileEntry && hasNumericAddressFamily(addrs, AF_INET) &&
      hasNumericAddressFamily(addrs, AF_INET6)) {
    auto selectedFamily = getNumericAddressFamily(ipaddr);
    auto nextFamily = getOppositeAddressFamily(selectedFamily);
    if (isSelectableAddressFamily(nextFamily) &&
        hasNumericAddressFamily(addrs, nextFamily)) {
      fileEntry->setNextAddressFamily(hostname, port,
                                      preferIPv4 ? AF_INET : nextFamily);
    }
  }
  return ipaddr;
}

namespace {
// Returns true if proxy is defined for the given protocol. Otherwise
// returns false.
bool isProxyRequest(const std::string& protocol,
                    const std::shared_ptr<Option>& option)
{
  std::string proxyUri = getProxyUri(protocol, option.get());
  return !proxyUri.empty();
}
} // namespace

namespace {
bool inNoProxy(const std::shared_ptr<Request>& req, const std::string& noProxy)
{
  std::vector<Scip> entries;
  util::splitIter(std::begin(noProxy), std::end(noProxy),
                  std::back_inserter(entries), ',', true);
  if (entries.empty()) {
    return false;
  }

  for (const auto& e : entries) {
    const auto slashpos = std::find(e.first, e.second, '/');
    if (slashpos == e.second) {
      if (util::noProxyDomainMatch(req->getHost(),
                                   std::string(e.first, e.second))) {
        return true;
      }

      continue;
    }
    // TODO We don't resolve hostname here.  More complete
    // implementation is that we should first resolve
    // hostname(which may result in several IP addresses) and
    // evaluates against all of them
    std::string ip(e.first, slashpos);
    uint32_t bits;
    if (!util::parseUIntNoThrow(bits, std::string(slashpos + 1, e.second))) {
      continue;
    }
    if (util::inSameCidrBlock(ip, req->getHost(), bits)) {
      return true;
    }
  }
  return false;
}
} // namespace

bool AbstractCommand::isProxyDefined() const
{
  return isProxyRequest(req_->getProtocol(), getOption()) &&
         !inNoProxy(req_, getOption()->get(PREF_NO_PROXY));
}

std::shared_ptr<Request> AbstractCommand::createProxyRequest() const
{
  std::shared_ptr<Request> proxyRequest;
  if (inNoProxy(req_, getOption()->get(PREF_NO_PROXY))) {
    return proxyRequest;
  }

  std::string proxy = getProxyUri(req_->getProtocol(), getOption().get());
  if (!proxy.empty()) {
    proxyRequest = std::make_shared<Request>();
    if (proxyRequest->setUri(proxy)) {
      A2_LOG_DEBUG(fmt("CUID#%" PRId64 " - Using proxy", getCuid()));
      A2_LOG_NETWORK(
          fmt("CUID#%" PRId64 " - Using proxy: %s", getCuid(),
              proxy.c_str()));
    }
    else {
      A2_LOG_DEBUG(
          fmt("CUID#%" PRId64 " - Failed to parse proxy string", getCuid()));
      A2_LOG_NETWORK(
          fmt("CUID#%" PRId64 " - Failed to parse proxy URI: %s", getCuid(),
              proxy.c_str()));
      proxyRequest.reset();
    }
  }
  return proxyRequest;
}

std::string AbstractCommand::resolveHostname(std::vector<std::string>& addrs,
                                             const std::string& hostname,
                                             uint16_t port)
{
  if (util::isNumericHost(hostname)) {
    addrs.push_back(hostname);
    return hostname;
  }

  auto mappedAddrs = getMappedAddresses(hostname, getOption().get());
  if (!mappedAddrs.empty()) {
    for (const auto& addr : mappedAddrs) {
      e_->cacheIPAddress(hostname, addr, port);
    }
    std::vector<std::string> cachedAddrs;
    e_->findAllCachedIPAddresses(std::back_inserter(cachedAddrs), hostname,
                                  port);
    for (const auto& addr : mappedAddrs) {
      if (std::find(std::begin(cachedAddrs), std::end(cachedAddrs), addr) !=
          std::end(cachedAddrs)) {
        addrs.push_back(addr);
      }
    }
    if (addrs.empty()) {
      throw DL_ABORT_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED,
                            "No host mapping address left to try"));
    }
    A2_LOG_INFO(fmt(MSG_DNS_CACHE_HIT, getCuid(), hostname.c_str(),
                    strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
    A2_LOG_NETWORK(
        fmt("DNS: hosts mapping %s -> %s", hostname.c_str(),
            strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
    auto ipaddr =
        selectIPAddress(addrs, getCuid(), getFileEntry(), hostname, port);
    prioritizeAndInterleaveIPAddress(addrs, ipaddr);
    return ipaddr;
  }

#ifdef ENABLE_ASYNC_DNS
  if (req_ && req_->getProtocol() == V_HTTPS && hostname == req_->getHost() &&
      port == req_->getPort()) {
    maybeStartHttpsServiceBindingDiscovery(e_, hostname, port,
                                           requestGroup_->getGID());
  }
#endif // ENABLE_ASYNC_DNS

  e_->findAllCachedIPAddresses(std::back_inserter(addrs), hostname, port);
  if (!addrs.empty()) {
#ifdef ENABLE_ASYNC_DNS
    if (getOption()->getAsBool(PREF_ASYNC_DNS)) {
      preserveAsyncDnsCacheFill(e_, hostname, port, asyncNameResolverMan_.get(),
                                requestGroup_, this);
      addrs.clear();
      e_->findAllCachedIPAddresses(std::back_inserter(addrs), hostname, port);
    }
#endif // ENABLE_ASYNC_DNS
    auto ipaddr =
        selectIPAddress(addrs, getCuid(), getFileEntry(), hostname, port);
    prioritizeAndInterleaveIPAddress(addrs, ipaddr);
    A2_LOG_INFO(fmt(MSG_DNS_CACHE_HIT, getCuid(), hostname.c_str(),
                    strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
    A2_LOG_NETWORK(
        fmt("DNS: cache hit %s -> %s", hostname.c_str(),
            strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
    return ipaddr;
  }

  std::string ipaddr;
#ifdef ENABLE_ASYNC_DNS
  bool asyncDnsUsed = false;
  if (getOption()->getAsBool(PREF_ASYNC_DNS)) {
    asyncDnsUsed = true;
    if (!asyncNameResolverMan_->started()) {
      asyncNameResolverMan_->startAsync(hostname, e_, this);
    }
    switch (asyncNameResolverMan_->getStatus()) {
    case -1:
      if (!isProxyRequest(req_->getProtocol(), getOption())) {
        e_->getRequestGroupMan()
            ->getOrCreateServerStat(req_->getHost(), req_->getProtocol())
            ->setError();
      }
      throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                             hostname.c_str(),
                             asyncNameResolverMan_->getLastError().c_str()),
                         error_code::NAME_RESOLVE_ERROR);
    case 0:
      return A2STR::NIL;

    case 1:
      asyncNameResolverMan_->getResolvedAddress(addrs);
      if (addrs.empty()) {
        throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                               hostname.c_str(), "No address returned"),
                           error_code::NAME_RESOLVE_ERROR);
      }
      A2_LOG_NETWORK(
          fmt("DNS: first usable async result for %s: %s",
              hostname.c_str(),
              strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
      break;
    }
  }
  else
#endif // ENABLE_ASYNC_DNS
  {
    NameResolver res;
    res.setSocktype(SOCK_STREAM);
    if (e_->getOption()->getAsBool(PREF_DISABLE_IPV6)) {
      res.setFamily(AF_INET);
    }
    res.resolve(addrs, hostname);
  }
  A2_LOG_INFO(fmt(MSG_NAME_RESOLUTION_COMPLETE, getCuid(), hostname.c_str(),
                  strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
  for (const auto& addr : addrs) {
    e_->cacheIPAddress(hostname, addr, port);
  }
#ifdef ENABLE_ASYNC_DNS
  if (asyncDnsUsed) {
    if (continueAsyncDnsCacheFill(e_, hostname, port,
                                  asyncNameResolverMan_.get(), requestGroup_,
                                  this)) {
      A2_LOG_NETWORK(
          fmt("DNS: keeping unfinished queries for %s in background",
              hostname.c_str()));
    }
  }
#endif // ENABLE_ASYNC_DNS
  std::vector<std::string> cachedAddrs;
  e_->findAllCachedIPAddresses(std::back_inserter(cachedAddrs), hostname,
                                port);
  addrs.swap(cachedAddrs);
  if (addrs.empty()) {
    throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                           hostname.c_str(), "No usable address returned"),
                       error_code::NAME_RESOLVE_ERROR);
  }
  ipaddr = selectIPAddress(addrs, getCuid(), getFileEntry(), hostname, port);
  prioritizeAndInterleaveIPAddress(addrs, ipaddr);
  return ipaddr;
}

void AbstractCommand::prepareForNextAction(
    std::unique_ptr<CheckIntegrityEntry> checkEntry)
{
  std::vector<std::unique_ptr<Command>> commands;
  requestGroup_->processCheckIntegrityEntry(commands, std::move(checkEntry),
                                            e_);
  e_->addCommand(std::move(commands));
  e_->setNoWait(true);
}

void AbstractCommand::refreshSegments()
{
  segments_.clear();
  getSegmentMan()->getInFlightSegment(segments_, getCuid());
}

bool AbstractCommand::checkIfConnectionEstablished(
    const std::shared_ptr<SocketCore>& socket,
    const std::string& connectedHostname, const std::string& connectedAddr,
    uint16_t connectedPort)
{
  std::string error = socket->getSocketError();
  if (error.empty()) {
    return true;
  }

  // See also InitiateConnectionCommand::executeInternal()
  e_->markBadIPAddress(connectedHostname, connectedAddr, connectedPort);
  if (e_->findCachedIPAddress(connectedHostname, connectedPort).empty()) {
    auto mappedAddrs =
        getMappedAddresses(connectedHostname, getOption().get());
    if (!mappedAddrs.empty()) {
      throw DL_ABORT_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED,
                            "No host mapping address left to try"));
    }
    e_->removeCachedIPAddress(connectedHostname, connectedPort);
    // Don't set error if proxy server is used and its method is GET.
    if (resolveProxyMethod(req_->getProtocol()) != V_GET ||
        !isProxyRequest(req_->getProtocol(), getOption())) {
      e_->getRequestGroupMan()
          ->getOrCreateServerStat(req_->getHost(), req_->getProtocol())
          ->setError();
    }
    throw DL_RETRY_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED, error.c_str()));
  }

  A2_LOG_INFO(fmt(MSG_CONNECT_FAILED_AND_RETRY, getCuid(),
                  connectedAddr.c_str(), connectedPort));
  A2_LOG_NETWORK(
      fmt("CUID#%" PRId64 " - Connection to %s:%u failed: %s, retrying",
          getCuid(), connectedAddr.c_str(), connectedPort, error.c_str()));
  e_->setNoWait(true);
  e_->addCommand(
      InitiateConnectionCommandFactory::createInitiateConnectionCommand(
          getCuid(), req_, fileEntry_, requestGroup_, e_));
  return false;
}

const std::string&
AbstractCommand::resolveProxyMethod(const std::string& protocol) const
{
  if (getOption()->get(PREF_PROXY_METHOD) == V_TUNNEL || protocol == "https" ||
      protocol == "sftp") {
    return V_TUNNEL;
  }
  return V_GET;
}

const std::shared_ptr<Option>& AbstractCommand::getOption() const
{
  return requestGroup_->getOption();
}

void AbstractCommand::createSocket()
{
  socket_ = std::make_shared<SocketCore>();
}

int32_t AbstractCommand::calculateMinSplitSize() const
{
  if (req_ && req_->isPipeliningEnabled()) {
    return getDownloadContext()->getPieceLength();
  }

  return getOption()->getAsInt(PREF_MIN_SPLIT_SIZE);
}

void AbstractCommand::setRequest(const std::shared_ptr<Request>& request)
{
  req_ = request;
}

void AbstractCommand::resetRequest() { req_.reset(); }

void AbstractCommand::setFileEntry(const std::shared_ptr<FileEntry>& fileEntry)
{
  fileEntry_ = fileEntry;
}

void AbstractCommand::setSocket(const std::shared_ptr<SocketCore>& s)
{
  socket_ = s;
}

const std::shared_ptr<DownloadContext>&
AbstractCommand::getDownloadContext() const
{
  return requestGroup_->getDownloadContext();
}

const std::shared_ptr<SegmentMan>& AbstractCommand::getSegmentMan() const
{
  return requestGroup_->getSegmentMan();
}

const std::shared_ptr<PieceStorage>& AbstractCommand::getPieceStorage() const
{
  return requestGroup_->getPieceStorage();
}

void AbstractCommand::checkSocketRecvBuffer()
{
  if (socketRecvBuffer_->bufferEmpty() &&
      socket_->getRecvBufferedLength() == 0) {
    return;
  }

  setStatus(Command::STATUS_ONESHOT_REALTIME);
  e_->setNoWait(true);
}

void AbstractCommand::addCommandSelf()
{
  e_->addCommand(std::unique_ptr<Command>(this));
}

} // namespace aria2
