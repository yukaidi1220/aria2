#include "AsyncNameResolver.h"
#ifdef ENABLE_SSL
#  include "AsyncDohNameResolver.h"
#  include "AsyncDotNameResolver.h"
#endif // ENABLE_SSL
#include "AsyncNameResolverMan.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "AsyncResolver.h"
#include "Command.h"
#include "Exception.h"
#include "File.h"
#include "LogFactory.h"
#include "Option.h"
#include "prefs.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "TestUtil.h"

namespace aria2 {

class AsyncNameResolverTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AsyncNameResolverTest);
  CPPUNIT_TEST(testGetQueryStatusBeforeStart);
  CPPUNIT_TEST(testStartAsyncStartsIPv4AndIPv6);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv6SucceedsAndIPv4Fails);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv6SucceedsAndIPv4IsPending);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv4SucceedsAndIPv6IsPending);
  CPPUNIT_TEST(testAsyncResolverWriteEventUsesExceptFd);
  CPPUNIT_TEST(testAsyncResolverExceptFdIsProcessedAsWriteReady);
  CPPUNIT_TEST(testValidateConfigLeavesCaresServersUnchanged);
  CPPUNIT_TEST(testStartAsyncCaresWithExplicitServerFallsBackToSystem);
  CPPUNIT_TEST(testCaresQueryLogsServerSource);
  CPPUNIT_TEST(testCaresRejectedServerLogsSystemFallback);
#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
  CPPUNIT_TEST(testCaresQueryLogsTcpTransport);
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS
#ifdef ENABLE_SSL
  CPPUNIT_TEST(testCreateDotResolver);
  CPPUNIT_TEST(testCreateDotResolverAcceptsDomainServer);
  CPPUNIT_TEST(testCreateDotResolverUsesIPv4BootstrapWhenIPv6Disabled);
  CPPUNIT_TEST(testCreateDotResolverRejectsEmptyServerList);
  CPPUNIT_TEST(testCreateDohResolver);
  CPPUNIT_TEST(testCreateDohResolverAcceptsDomainServer);
  CPPUNIT_TEST(testCreateDohResolverUsesUnspecBootstrapForDualStack);
  CPPUNIT_TEST(testCreateDohResolverRejectsEmptyServerList);
  CPPUNIT_TEST(testStartAsyncDotFallsBackToSystem);
  CPPUNIT_TEST(testConfigureDohHttp2AffectsLoggedTransport);
  CPPUNIT_TEST(testStartAsyncMultiLogsQueryPlans);
  CPPUNIT_TEST(testStartAsyncMultiStartsSecureBackendsBeforePlainFallback);
  CPPUNIT_TEST(testStartAsyncMultiLimitsSecureWindowForSingleStack);
  CPPUNIT_TEST(testStartAsyncMultiLimitsSecureWindowForDualStack);
  CPPUNIT_TEST(testStartAsyncMultiLimitsPlainWindowForDualStack);
  CPPUNIT_TEST(testStartAsyncMultiFallsBackToExplicitPlainThenSystem);
  CPPUNIT_TEST(testStartAsyncMultiAllFakeDnsExhaustsFallbacks);
  CPPUNIT_TEST(testStartAsyncMultiSecureOnlySkipsPlainFallback);
  CPPUNIT_TEST(testStartAsyncMultiPlainOnlySkipsSecureBackends);
  CPPUNIT_TEST(testValidateConfigAcceptsDotIpServers);
  CPPUNIT_TEST(testValidateConfigAcceptsDotDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDotEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsDohIpServers);
  CPPUNIT_TEST(testValidateConfigAcceptsDohDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDohEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsMultiServers);
  CPPUNIT_TEST(testParseMultiTreatsBareIpAsPlainUdp);
  CPPUNIT_TEST(testParseMultiRequiresDotPrefixPerDotServer);
  CPPUNIT_TEST(testValidateConfigRejectsMultiPlainDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsMultiBadPlainPort);
  CPPUNIT_TEST(testPlainBootstrapFactoryUsesConfiguredPlainServers);
  CPPUNIT_TEST(testConfigureAcceptsDotIpServers);
  CPPUNIT_TEST(testConfigureAcceptsDotDomainServer);
  CPPUNIT_TEST(testConfigureRejectsDotEmptyServerList);
  CPPUNIT_TEST(testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled);
  CPPUNIT_TEST(testConfigureAcceptsDohIpServers);
  CPPUNIT_TEST(testConfigureAcceptsDohDomainServer);
  CPPUNIT_TEST(testConfigureRejectsDohEmptyServerList);
  CPPUNIT_TEST(testConfigureAcceptsMultiServers);
  CPPUNIT_TEST(testConfigureRejectsMultiPlainDomainServer);
#endif // ENABLE_SSL
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}

  void tearDown() {}

  void testGetQueryStatusBeforeStart();
  void testStartAsyncStartsIPv4AndIPv6();
  void testGetStatusSucceedsWhenIPv6SucceedsAndIPv4Fails();
  void testGetStatusSucceedsWhenIPv6SucceedsAndIPv4IsPending();
  void testGetStatusSucceedsWhenIPv4SucceedsAndIPv6IsPending();
  void testAsyncResolverWriteEventUsesExceptFd();
  void testAsyncResolverExceptFdIsProcessedAsWriteReady();
  void testValidateConfigLeavesCaresServersUnchanged();
  void testStartAsyncCaresWithExplicitServerFallsBackToSystem();
  void testCaresQueryLogsServerSource();
  void testCaresRejectedServerLogsSystemFallback();
#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
  void testCaresQueryLogsTcpTransport();
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS
#ifdef ENABLE_SSL
  void testCreateDotResolver();
  void testCreateDotResolverAcceptsDomainServer();
  void testCreateDotResolverUsesIPv4BootstrapWhenIPv6Disabled();
  void testCreateDotResolverRejectsEmptyServerList();
  void testCreateDohResolver();
  void testCreateDohResolverAcceptsDomainServer();
  void testCreateDohResolverUsesUnspecBootstrapForDualStack();
  void testCreateDohResolverRejectsEmptyServerList();
  void testStartAsyncDotFallsBackToSystem();
  void testConfigureDohHttp2AffectsLoggedTransport();
  void testStartAsyncMultiLogsQueryPlans();
  void testStartAsyncMultiStartsSecureBackendsBeforePlainFallback();
  void testStartAsyncMultiLimitsSecureWindowForSingleStack();
  void testStartAsyncMultiLimitsSecureWindowForDualStack();
  void testStartAsyncMultiLimitsPlainWindowForDualStack();
  void testStartAsyncMultiFallsBackToExplicitPlainThenSystem();
  void testStartAsyncMultiAllFakeDnsExhaustsFallbacks();
  void testStartAsyncMultiSecureOnlySkipsPlainFallback();
  void testStartAsyncMultiPlainOnlySkipsSecureBackends();
  void testValidateConfigAcceptsDotIpServers();
  void testValidateConfigAcceptsDotDomainServer();
  void testValidateConfigRejectsDotEmptyServerList();
  void testValidateConfigAcceptsDohIpServers();
  void testValidateConfigAcceptsDohDomainServer();
  void testValidateConfigRejectsDohEmptyServerList();
  void testValidateConfigAcceptsMultiServers();
  void testParseMultiTreatsBareIpAsPlainUdp();
  void testParseMultiRequiresDotPrefixPerDotServer();
  void testValidateConfigRejectsMultiPlainDomainServer();
  void testValidateConfigRejectsMultiBadPlainPort();
  void testPlainBootstrapFactoryUsesConfiguredPlainServers();
  void testConfigureAcceptsDotIpServers();
  void testConfigureAcceptsDotDomainServer();
  void testConfigureRejectsDotEmptyServerList();
  void testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled();
  void testConfigureAcceptsDohIpServers();
  void testConfigureAcceptsDohDomainServer();
  void testConfigureRejectsDohEmptyServerList();
  void testConfigureAcceptsMultiServers();
  void testConfigureRejectsMultiPlainDomainServer();
#endif // ENABLE_SSL
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncNameResolverTest);

namespace {

class MockAsyncResolver : public AsyncResolver {
private:
  int family_;
  STATUS status_;
  std::vector<std::string> addrs_;
  std::string error_;
  std::string hostname_;
  std::vector<AsyncResolverSocketEntry> socks_;
  bool usable_;

public:
  MockAsyncResolver(int family, STATUS status, std::vector<std::string> addrs,
                    std::string error = std::string(), bool usable = false)
      : family_(family),
        status_(status),
        addrs_(std::move(addrs)),
        error_(std::move(error)),
        usable_(usable)
  {
  }

  void resolve(const std::string& name) CXX11_OVERRIDE { hostname_ = name; }

  const std::vector<std::string>& getResolvedAddresses() const CXX11_OVERRIDE
  {
    return addrs_;
  }

  const std::string& getError() const CXX11_OVERRIDE { return error_; }

  STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  bool usable() const CXX11_OVERRIDE { return usable_; }

  int getFamily() const CXX11_OVERRIDE { return family_; }

  const std::vector<AsyncResolverSocketEntry>& getsock() const CXX11_OVERRIDE
  {
    return socks_;
  }

  void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE
  {
    (void)readfd;
    (void)writefd;
  }

  const std::string& getHostname() const CXX11_OVERRIDE { return hostname_; }
};

class MockAsyncNameResolverMan : public AsyncNameResolverMan {
private:
  std::shared_ptr<AsyncResolver> ipv6Resolver_;
  std::shared_ptr<AsyncResolver> ipv4Resolver_;

public:
  MockAsyncNameResolverMan(std::shared_ptr<AsyncResolver> ipv6Resolver,
                           std::shared_ptr<AsyncResolver> ipv4Resolver)
      : ipv6Resolver_(std::move(ipv6Resolver)),
        ipv4Resolver_(std::move(ipv4Resolver))
  {
  }

  std::shared_ptr<AsyncResolver> createResolver(int family) const CXX11_OVERRIDE
  {
    if (family == AF_INET6) {
      return ipv6Resolver_;
    }
    return ipv4Resolver_;
  }
};

class MockFallbackAsyncNameResolverMan : public AsyncNameResolverMan {
private:
  mutable size_t createResolversCalls_;
  std::vector<size_t> resolverCounts_;

public:
  MockFallbackAsyncNameResolverMan() : createResolversCalls_(0)
  {
    resolverCounts_.push_back(2);
    resolverCounts_.push_back(2);
    resolverCounts_.push_back(1);
  }

  explicit MockFallbackAsyncNameResolverMan(std::vector<size_t> resolverCounts)
      : createResolversCalls_(0), resolverCounts_(std::move(resolverCounts))
  {
  }

  size_t getCreateResolversCalls() const { return createResolversCalls_; }

  std::vector<std::shared_ptr<AsyncResolver>>
  createResolvers(int family) const CXX11_OVERRIDE
  {
    size_t resolverCount = 1;
    if (!resolverCounts_.empty()) {
      auto index = createResolversCalls_;
      if (index >= resolverCounts_.size()) {
        index = resolverCounts_.size() - 1;
      }
      resolverCount = resolverCounts_[index];
    }
    ++createResolversCalls_;
    std::vector<std::shared_ptr<AsyncResolver>> resolvers;
    for (size_t i = 0; i < resolverCount; ++i) {
      resolvers.push_back(std::make_shared<MockAsyncResolver>(
          family, AsyncResolver::STATUS_ERROR, std::vector<std::string>(),
          "mock resolver failed"));
    }
    return resolvers;
  }
};

#ifdef ENABLE_SSL
class InspectableAsyncNameResolverMan : public AsyncNameResolverMan {
public:
  using AsyncNameResolverMan::createResolvers;
};
#endif // ENABLE_SSL

class ScopedNetworkLog {
public:
  explicit ScopedNetworkLog(std::string path) : path_(std::move(path))
  {
    File(path_).remove();
    LogFactory::setLogFile(path_);
    LogFactory::setLogLevel(V_NETWORK);
    LogFactory::reconfigure();
  }

  ~ScopedNetworkLog()
  {
    LogFactory::setLogFile("");
    LogFactory::setLogLevel(Logger::A2_DEBUG);
    LogFactory::reconfigure();
  }

  std::string closeAndRead() const
  {
    LogFactory::setLogFile("");
    LogFactory::reconfigure();
    return readFile(path_);
  }

private:
  std::string path_;
};

class MockCommand : public Command {
public:
  explicit MockCommand(cuid_t cuid) : Command(cuid) {}

  bool execute() CXX11_OVERRIDE { return true; }
};

size_t countQueryStatusEntries(const std::string& status)
{
  if (status.empty()) {
    return 0;
  }

  size_t count = 1;
  std::string::size_type pos = 0;
  while ((pos = status.find(", ", pos)) != std::string::npos) {
    ++count;
    pos += 2;
  }
  return count;
}

size_t countSubstring(const std::string& haystack, const std::string& needle)
{
  size_t count = 0;
  std::string::size_type pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

} // namespace

void AsyncNameResolverTest::testGetQueryStatusBeforeStart()
{
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_EQUAL(std::string(), resolverMan.getQueryStatus());
}

void AsyncNameResolverTest::testStartAsyncStartsIPv4AndIPv6()
{
  std::vector<std::string> ipv6Addrs;
  auto ipv6Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET6, AsyncResolver::STATUS_QUERYING, ipv6Addrs);
  std::vector<std::string> ipv4Addrs;
  auto ipv4Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, ipv4Addrs);
  MockAsyncNameResolverMan resolverMan(ipv6Resolver, ipv4Resolver);
  MockCommand command(1);

  resolverMan.startAsync("dual.example", nullptr, &command);

  CPPUNIT_ASSERT(resolverMan.started());
  CPPUNIT_ASSERT_EQUAL(std::string("dual.example"),
                       ipv6Resolver->getHostname());
  CPPUNIT_ASSERT_EQUAL(std::string("dual.example"),
                       ipv4Resolver->getHostname());
}

void AsyncNameResolverTest::testGetStatusSucceedsWhenIPv6SucceedsAndIPv4Fails()
{
  std::vector<std::string> ipv6Addrs;
  ipv6Addrs.push_back("2001:db8::1");
  auto ipv6Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET6, AsyncResolver::STATUS_SUCCESS, ipv6Addrs);
  std::vector<std::string> ipv4Addrs;
  auto ipv4Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_ERROR, ipv4Addrs, "A failed");
  MockAsyncNameResolverMan resolverMan(ipv6Resolver, ipv4Resolver);
  MockCommand command(1);

  resolverMan.startAsync("dual.example", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(1, resolverMan.getStatus());
  std::vector<std::string> resolvedAddrs;
  resolverMan.getResolvedAddress(resolvedAddrs);
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolvedAddrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), resolvedAddrs[0]);
}

void AsyncNameResolverTest::testGetStatusSucceedsWhenIPv6SucceedsAndIPv4IsPending()
{
  std::vector<std::string> ipv6Addrs;
  ipv6Addrs.push_back("2001:db8::1");
  auto ipv6Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET6, AsyncResolver::STATUS_SUCCESS, ipv6Addrs);
  std::vector<std::string> ipv4Addrs;
  auto ipv4Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, ipv4Addrs, std::string(), true);
  MockAsyncNameResolverMan resolverMan(ipv6Resolver, ipv4Resolver);
  MockCommand command(1);

  resolverMan.startAsync("dual.example", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(1, resolverMan.getStatus());
  std::vector<std::string> resolvedAddrs;
  resolverMan.getResolvedAddress(resolvedAddrs);
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolvedAddrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), resolvedAddrs[0]);

  auto pendingResolvers = resolverMan.detachPendingResolvers(nullptr, nullptr);
  CPPUNIT_ASSERT_EQUAL((size_t)1, pendingResolvers.size());
  CPPUNIT_ASSERT_EQUAL(AF_INET, pendingResolvers[0]->getFamily());
}

void AsyncNameResolverTest::testGetStatusSucceedsWhenIPv4SucceedsAndIPv6IsPending()
{
  std::vector<std::string> ipv6Addrs;
  auto ipv6Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET6, AsyncResolver::STATUS_QUERYING, ipv6Addrs, std::string(),
      true);
  std::vector<std::string> ipv4Addrs;
  ipv4Addrs.push_back("192.0.2.1");
  auto ipv4Resolver = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_SUCCESS, ipv4Addrs);
  MockAsyncNameResolverMan resolverMan(ipv6Resolver, ipv4Resolver);
  MockCommand command(1);

  resolverMan.startAsync("dual.example", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(1, resolverMan.getStatus());
  std::vector<std::string> resolvedAddrs;
  resolverMan.getResolvedAddress(resolvedAddrs);
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolvedAddrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), resolvedAddrs[0]);

  auto pendingResolvers = resolverMan.detachPendingResolvers(nullptr, nullptr);
  CPPUNIT_ASSERT_EQUAL((size_t)1, pendingResolvers.size());
  CPPUNIT_ASSERT_EQUAL(AF_INET6, pendingResolvers[0]->getFamily());
}

void AsyncNameResolverTest::testAsyncResolverWriteEventUsesExceptFd()
{
  fd_set rfds;
  fd_set wfds;
  fd_set efds;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);
  AsyncResolverSocketEntry ent{77, EventPoll::EVENT_WRITE};

  auto nfds = addAsyncResolverSocketEntryFdSet(ent, &rfds, &wfds, &efds);

  CPPUNIT_ASSERT_EQUAL(static_cast<sock_t>(78), nfds);
  CPPUNIT_ASSERT(!FD_ISSET(ent.fd, &rfds));
  CPPUNIT_ASSERT(FD_ISSET(ent.fd, &wfds));
  CPPUNIT_ASSERT(FD_ISSET(ent.fd, &efds));
}

void AsyncNameResolverTest::testAsyncResolverExceptFdIsProcessedAsWriteReady()
{
  fd_set rfds;
  fd_set wfds;
  fd_set efds;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);
  AsyncResolverSocketEntry ent{77, EventPoll::EVENT_WRITE};
  FD_SET(ent.fd, &efds);
  auto readfd = AsyncResolver::badSocket();
  auto writefd = AsyncResolver::badSocket();

  getReadyAsyncResolverSocketEntryFds(ent, &rfds, &wfds, &efds, readfd,
                                      writefd);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), readfd);
  CPPUNIT_ASSERT_EQUAL(ent.fd, writefd);
}

void AsyncNameResolverTest::testValidateConfigLeavesCaresServersUnchanged()
{
  validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_CARES,
                                  "dns.example.org");
}

void AsyncNameResolverTest::testStartAsyncCaresWithExplicitServerFallsBackToSystem()
{
  MockFallbackAsyncNameResolverMan resolverMan({1, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_CARES);
  resolverMan.setServers("192.0.2.53");
  resolverMan.setIPv6(false);
  MockCommand command(1);

  resolverMan.startAsync("example.org", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
}

void AsyncNameResolverTest::testCaresQueryLogsServerSource()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testCaresQueryLogsServerSource.log";
  ScopedNetworkLog log(logPath);

  {
    AsyncNameResolver resolver(AF_INET, std::string());
    resolver.resolve("system.example");
  }

  {
    AsyncNameResolver resolver(AF_INET6, "192.0.2.53");
    resolver.resolve("configured.example");
  }

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: query A system.example using c-ares "
                           "server_source=system servers=system "
                           "transport=UDP") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: query AAAA configured.example using c-ares "
                           "server_source=configured servers=192.0.2.53 "
                           "transport=UDP") != std::string::npos);
}

void AsyncNameResolverTest::testCaresRejectedServerLogsSystemFallback()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testCaresRejectedServerLogsSystemFallback.log";
  ScopedNetworkLog log(logPath);

  AsyncNameResolver resolver(AF_INET, "dns.example.org");
  resolver.resolve("rejected.example");

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: c-ares rejected configured server list "
                           "dns.example.org:") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: query A rejected.example using c-ares "
                           "server_source=system servers=system "
                           "transport=UDP") != std::string::npos);
}

#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
void AsyncNameResolverTest::testCaresQueryLogsTcpTransport()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testCaresQueryLogsTcpTransport.log";
  ScopedNetworkLog log(logPath);

  AsyncNameResolver resolver(AF_INET, "192.0.2.53", true);
  resolver.resolve("tcp.example");

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: c-ares plain resolver forcing TCP "
                           "transport") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: query A tcp.example using c-ares "
                           "server_source=configured servers=192.0.2.53 "
                           "transport=TCP") != std::string::npos);
}
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS

#ifdef ENABLE_SSL
void AsyncNameResolverTest::testCreateDotResolver()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);
  resolverMan.setServers("1.1.1.1");

  auto resolver = resolverMan.createResolver(AF_INET);

  CPPUNIT_ASSERT(dynamic_cast<AsyncDotNameResolver*>(resolver.get()));
  CPPUNIT_ASSERT_EQUAL(AF_INET, resolver->getFamily());
}

void AsyncNameResolverTest::testCreateDotResolverAcceptsDomainServer()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);
  resolverMan.setServers("dns.example.org");

  auto resolver = resolverMan.createResolver(AF_INET);

  CPPUNIT_ASSERT(dynamic_cast<AsyncDotNameResolver*>(resolver.get()));
  CPPUNIT_ASSERT_EQUAL(AF_INET, resolver->getFamily());
}

void AsyncNameResolverTest::testCreateDotResolverUsesIPv4BootstrapWhenIPv6Disabled()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);
  resolverMan.setServers("dns.example.org");
  resolverMan.setIPv6(false);

  auto resolver = resolverMan.createResolver(AF_INET);

  auto dotResolver = dynamic_cast<AsyncDotNameResolver*>(resolver.get());
  CPPUNIT_ASSERT(dotResolver);
  CPPUNIT_ASSERT_EQUAL(AF_INET, dotResolver->getBootstrapFamily());
}

void AsyncNameResolverTest::testCreateDotResolverRejectsEmptyServerList()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);

  CPPUNIT_ASSERT_THROW(resolverMan.createResolver(AF_INET), Exception);
}

void AsyncNameResolverTest::testCreateDohResolver()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);
  resolverMan.setServers("https://1.1.1.1/dns-query");

  auto resolver = resolverMan.createResolver(AF_INET);

  CPPUNIT_ASSERT(dynamic_cast<AsyncDohNameResolver*>(resolver.get()));
  CPPUNIT_ASSERT_EQUAL(AF_INET, resolver->getFamily());
}

void AsyncNameResolverTest::testCreateDohResolverAcceptsDomainServer()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);
  resolverMan.setServers("https://dns.example.org/dns-query");

  auto resolver = resolverMan.createResolver(AF_INET);

  CPPUNIT_ASSERT(dynamic_cast<AsyncDohNameResolver*>(resolver.get()));
  CPPUNIT_ASSERT_EQUAL(AF_INET, resolver->getFamily());
}

void AsyncNameResolverTest::testCreateDohResolverUsesUnspecBootstrapForDualStack()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);
  resolverMan.setServers("https://dns.example.org/dns-query");

  auto resolver = resolverMan.createResolver(AF_INET);

  auto dohResolver = dynamic_cast<AsyncDohNameResolver*>(resolver.get());
  CPPUNIT_ASSERT(dohResolver);
  CPPUNIT_ASSERT_EQUAL(AF_UNSPEC, dohResolver->getBootstrapFamily());
}

void AsyncNameResolverTest::testCreateDohResolverRejectsEmptyServerList()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);

  CPPUNIT_ASSERT_THROW(resolverMan.createResolver(AF_INET), Exception);
}

void AsyncNameResolverTest::testStartAsyncDotFallsBackToSystem()
{
  MockFallbackAsyncNameResolverMan resolverMan({1, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);
  resolverMan.setServers("dns.example.org");
  resolverMan.setIPv6(false);
  MockCommand command(1);

  resolverMan.startAsync("example.org", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
}

void AsyncNameResolverTest::testConfigureDohHttp2AffectsLoggedTransport()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testConfigureDohHttp2AffectsLoggedTransport.log";
  ScopedNetworkLog log(logPath);

  {
    Option option;
    option.put(PREF_ASYNC_DNS, A2_V_TRUE);
    option.put(PREF_DISABLE_IPV6, A2_V_TRUE);
    option.put(PREF_ASYNC_DNS_MODE, V_DOH);
    option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");
    option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);
    option.put(PREF_ENABLE_HTTP_PIPELINING, A2_V_FALSE);
    MockFallbackAsyncNameResolverMan resolverMan({1});

    configureAsyncNameResolverMan(&resolverMan, &option);
    resolverMan.setIPv4(true);
    resolverMan.setIPv6(false);
    MockCommand command(101);
    resolverMan.startAsync("example.org", nullptr, &command);
  }

  {
    Option option;
    option.put(PREF_ASYNC_DNS, A2_V_TRUE);
    option.put(PREF_DISABLE_IPV6, A2_V_TRUE);
    option.put(PREF_ASYNC_DNS_MODE, V_DOH);
    option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");
    option.put(PREF_ENABLE_HTTP2, A2_V_FALSE);
    option.put(PREF_ENABLE_DOH_HTTP2, A2_V_TRUE);
    option.put(PREF_ENABLE_HTTP_PIPELINING, A2_V_FALSE);
    MockFallbackAsyncNameResolverMan resolverMan({1});

    configureAsyncNameResolverMan(&resolverMan, &option);
    resolverMan.setIPv4(true);
    resolverMan.setIPv6(false);
    MockCommand command(102);
    resolverMan.startAsync("example.org", nullptr, &command);
  }

  {
    Option option;
    option.put(PREF_ASYNC_DNS, A2_V_TRUE);
    option.put(PREF_DISABLE_IPV6, A2_V_TRUE);
    option.put(PREF_ASYNC_DNS_MODE, V_DOH);
    option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");
    option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);
    option.put(PREF_ENABLE_HTTP_PIPELINING, A2_V_TRUE);
    MockFallbackAsyncNameResolverMan resolverMan({1});

    configureAsyncNameResolverMan(&resolverMan, &option);
    resolverMan.setIPv4(true);
    resolverMan.setIPv6(false);
    MockCommand command(103);
    resolverMan.startAsync("example.org", nullptr, &command);
  }

  auto logs = log.closeAndRead();
#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(logs.find("DNS: CUID#101 - query plan host=example.org "
                           "qtype=A mode=doh phase=primary backend=DoH "
                           "transport=https-h1-or-h2 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=system-cares fallback_from=none") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: CUID#102 - query plan host=example.org "
                           "qtype=A mode=doh phase=primary backend=DoH "
                           "transport=https-h1-or-h2 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=system-cares fallback_from=none") !=
                 std::string::npos);
#else  // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(logs.find("DNS: CUID#101 - query plan host=example.org "
                           "qtype=A mode=doh phase=primary backend=DoH "
                           "transport=https-h1 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=system-cares fallback_from=none") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: CUID#102 - query plan host=example.org "
                           "qtype=A mode=doh phase=primary backend=DoH "
                           "transport=https-h1 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=system-cares fallback_from=none") !=
                 std::string::npos);
#endif // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(logs.find("DNS: CUID#103 - query plan host=example.org "
                           "qtype=A mode=doh phase=primary backend=DoH "
                           "transport=https-h1 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=system-cares fallback_from=none") !=
                 std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiLogsQueryPlans()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiLogsQueryPlans.log";
  ScopedNetworkLog log(logPath);
  MockFallbackAsyncNameResolverMan resolverMan({2, 2, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("udp://192.0.2.53,tcp://192.0.2.54,"
                         "dot://dns.example.org,"
                         "https://dns.example.org/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(99);

  resolverMan.startAsync("example.org", nullptr, &command);
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: CUID#99 - query plan host=example.org "
                           "qtype=A mode=multi phase=primary backend=DoT "
                           "transport=tls server=dns.example.org:853 "
                           "bootstrap=explicit-plain-dns "
                           "fallback_from=none") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("qtype=A mode=multi phase=primary backend=DoH "
                           "transport=https-h1 "
                           "server=https://dns.example.org:443/dns-query "
                           "bootstrap=explicit-plain-dns "
                           "fallback_from=none") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("qtype=A mode=multi "
                           "phase=explicit-plain-fallback backend=c-ares "
                           "transport=udp server=192.0.2.53 bootstrap=none "
                           "fallback_from=secure-dns") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("qtype=A mode=multi "
                           "phase=explicit-plain-fallback backend=c-ares "
                           "transport=tcp server=192.0.2.54 bootstrap=none "
                           "fallback_from=secure-dns") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("qtype=A mode=multi "
                           "phase=system-cares-fallback backend=c-ares "
                           "transport=system-cares server=system "
                           "bootstrap=none "
                           "fallback_from=explicit-plain-dns") !=
                 std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiStartsSecureBackendsBeforePlainFallback()
{
  InspectableAsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers(
      "udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,"
      "https://dns.example.org/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(1);

  auto resolvers = resolverMan.createResolvers(AF_INET);
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolvers.size());
  CPPUNIT_ASSERT_EQUAL(AF_INET, resolvers[0]->getFamily());

  resolverMan.startAsync("example.org", nullptr, &command);

  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)1, countQueryStatusEntries(status));
  CPPUNIT_ASSERT(status.find("A=") != std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiLimitsSecureWindowForSingleStack()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiLimitsSecureWindowForSingleStack.log";
  ScopedNetworkLog log(logPath);
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("dot://192.0.2.1,https://192.0.2.2/dns-query,"
                         "dot://192.0.2.3,https://192.0.2.4/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(106);

  resolverMan.startAsync("single-stack.example", nullptr, &command);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT_EQUAL(
      (size_t)2,
      countSubstring(logs, "DNS: multi secure DNS window activated"));
  CPPUNIT_ASSERT(logs.find("multi secure DNS window activated candidate=1 "
                           "active_limit=2") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("multi secure DNS window activated candidate=2 "
                           "active_limit=2") != std::string::npos);
  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)1, countQueryStatusEntries(status));
}

void AsyncNameResolverTest::testStartAsyncMultiLimitsSecureWindowForDualStack()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiLimitsSecureWindowForDualStack.log";
  ScopedNetworkLog log(logPath);
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("dot://192.0.2.1,https://192.0.2.2/dns-query,"
                         "dot://192.0.2.3,https://192.0.2.4/dns-query");
  MockCommand command(107);

  resolverMan.startAsync("dual-stack.example", nullptr, &command);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT_EQUAL(
      (size_t)2,
      countSubstring(logs, "DNS: multi secure DNS window activated"));
  CPPUNIT_ASSERT_EQUAL(
      (size_t)2,
      countSubstring(logs, "multi secure DNS window activated candidate="));
  CPPUNIT_ASSERT_EQUAL(
      (size_t)1,
      countSubstring(logs, "multi secure DNS window activated candidate=1 "
                           "active_limit=1"));
  CPPUNIT_ASSERT_EQUAL(
      (size_t)1,
      countSubstring(logs, "multi secure DNS window activated candidate=2 "
                           "active_limit=1"));
  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)2, countQueryStatusEntries(status));
  CPPUNIT_ASSERT(status.find("A=") != std::string::npos);
  CPPUNIT_ASSERT(status.find("AAAA=") != std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiLimitsPlainWindowForDualStack()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiLimitsPlainWindowForDualStack.log";
  ScopedNetworkLog log(logPath);
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("udp://192.0.2.53,udp://192.0.2.54,"
                         "udp://192.0.2.55,udp://192.0.2.56");
  MockCommand command(108);

  resolverMan.startAsync("plain-window.example", nullptr, &command);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT_EQUAL(
      (size_t)2,
      countSubstring(logs, "DNS: multi plain DNS window activated"));
  CPPUNIT_ASSERT_EQUAL(
      (size_t)2,
      countSubstring(logs, "active_limit=1"));
  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)2, countQueryStatusEntries(status));
}

void AsyncNameResolverTest::testStartAsyncMultiFallsBackToExplicitPlainThenSystem()
{
  MockFallbackAsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("udp://192.0.2.53,tcp://192.0.2.54,"
                         "dot://dns.example.org,"
                         "https://dns.example.org/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(1);

  resolverMan.startAsync("example.org", nullptr, &command);

  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)2,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)2,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)1, countQueryStatusEntries(status));
  CPPUNIT_ASSERT(status.find("A=") != std::string::npos);
  CPPUNIT_ASSERT_EQUAL((size_t)3, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
}

void AsyncNameResolverTest::testStartAsyncMultiAllFakeDnsExhaustsFallbacks()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiAllFakeDnsExhaustsFallbacks.log";
  ScopedNetworkLog log(logPath);
  MockFallbackAsyncNameResolverMan resolverMan({2, 2, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("udp://192.0.2.53,tcp://192.0.2.54,"
                         "dot://dns.example.org,"
                         "https://dns.example.org/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(103);

  resolverMan.startAsync("fake.example", nullptr, &command);
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT_EQUAL((size_t)2,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));

  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT_EQUAL((size_t)2,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));

  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)3, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT_EQUAL((size_t)1,
                       countQueryStatusEntries(resolverMan.getQueryStatus()));

  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL((size_t)3, resolverMan.getCreateResolversCalls());

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: secure DNS failed; falling back to "
                           "explicit plain DNS for fake.example") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: explicit plain DNS failed; falling back to "
                           "system c-ares DNS for fake.example") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("no further async DNS fallback") ==
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("phase=explicit-plain-fallback backend=c-ares "
                           "transport=udp server=192.0.2.53") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("phase=system-cares-fallback backend=c-ares "
                           "transport=system-cares server=system") !=
                 std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiSecureOnlySkipsPlainFallback()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiSecureOnlySkipsPlainFallback.log";
  ScopedNetworkLog log(logPath);
  MockFallbackAsyncNameResolverMan resolverMan({2, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("dot://dns.example.org,"
                         "https://dns.example.org/dns-query");
  resolverMan.setIPv6(false);
  MockCommand command(104);

  resolverMan.startAsync("secure-only.example", nullptr, &command);
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());

  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: secure DNS failed; falling back to "
                           "system c-ares DNS for secure-only.example") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("phase=explicit-plain-fallback") ==
                 std::string::npos);
}

void AsyncNameResolverTest::testStartAsyncMultiPlainOnlySkipsSecureBackends()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testStartAsyncMultiPlainOnlySkipsSecureBackends.log";
  ScopedNetworkLog log(logPath);
  MockFallbackAsyncNameResolverMan resolverMan({2, 1});
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_MULTI);
  resolverMan.setServers("udp://192.0.2.53,tcp://192.0.2.54");
  resolverMan.setIPv6(false);
  MockCommand command(105);

  resolverMan.startAsync("plain-only.example", nullptr, &command);
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolverMan.getCreateResolversCalls());

  CPPUNIT_ASSERT(resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL(-1, resolverMan.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());
  CPPUNIT_ASSERT(!resolverMan.startFallback(nullptr, &command));
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolverMan.getCreateResolversCalls());

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("phase=primary backend=c-ares transport=udp "
                           "server=192.0.2.53") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("phase=primary backend=c-ares transport=tcp "
                           "server=192.0.2.54") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: explicit plain DNS failed; falling back to "
                           "system c-ares DNS for plain-only.example") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("backend=DoT") == std::string::npos);
  CPPUNIT_ASSERT(logs.find("backend=DoH") == std::string::npos);
}

void AsyncNameResolverTest::testValidateConfigAcceptsDotIpServers()
{
  validateAsyncNameResolverConfig(
      AsyncNameResolverMan::RESOLVER_DOT,
      "1.1.1.1,[2606:4700:4700::1111]:853");
}

void AsyncNameResolverTest::testValidateConfigAcceptsDotDomainServer()
{
  validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOT,
                                  "dns.example.org");
}

void AsyncNameResolverTest::testValidateConfigRejectsDotEmptyServerList()
{
  CPPUNIT_ASSERT_THROW(
      validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOT, ""),
      Exception);
}

void AsyncNameResolverTest::testValidateConfigAcceptsDohIpServers()
{
  validateAsyncNameResolverConfig(
      AsyncNameResolverMan::RESOLVER_DOH,
      "https://1.1.1.1/dns-query,"
      "https://[2606:4700:4700::1111]:443/dns-query");
}

void AsyncNameResolverTest::testValidateConfigAcceptsDohDomainServer()
{
  validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOH,
                                  "https://dns.example.org/dns-query");
}

void AsyncNameResolverTest::testValidateConfigRejectsDohEmptyServerList()
{
  CPPUNIT_ASSERT_THROW(
      validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOH, ""),
      Exception);
}

void AsyncNameResolverTest::testValidateConfigAcceptsMultiServers()
{
  validateAsyncNameResolverConfig(
      AsyncNameResolverMan::RESOLVER_MULTI,
      "udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,"
      "https://dns.example.org/dns-query,8.8.8.8");
}

void AsyncNameResolverTest::testParseMultiTreatsBareIpAsPlainUdp()
{
  auto config =
      parseAsyncDnsMultiServerConfigList("dot://223.6.6.6,180.184.1.1");

  CPPUNIT_ASSERT_EQUAL((size_t)1, config.dotServers.size());
  CPPUNIT_ASSERT_EQUAL(std::string("223.6.6.6"),
                       config.dotServers[0].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.dotServers[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string("180.184.1.1"), config.udpServers);
  CPPUNIT_ASSERT(config.tcpServers.empty());
  CPPUNIT_ASSERT(config.dohServers.empty());
}

void AsyncNameResolverTest::testParseMultiRequiresDotPrefixPerDotServer()
{
  auto config =
      parseAsyncDnsMultiServerConfigList("dot://223.6.6.6,dot://180.184.1.1");

  CPPUNIT_ASSERT_EQUAL((size_t)2, config.dotServers.size());
  CPPUNIT_ASSERT_EQUAL(std::string("223.6.6.6"),
                       config.dotServers[0].connectHost);
  CPPUNIT_ASSERT_EQUAL(std::string("180.184.1.1"),
                       config.dotServers[1].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.dotServers[1].port);
  CPPUNIT_ASSERT(config.udpServers.empty());
  CPPUNIT_ASSERT(config.tcpServers.empty());
  CPPUNIT_ASSERT(config.dohServers.empty());
}

void AsyncNameResolverTest::testValidateConfigRejectsMultiPlainDomainServer()
{
  CPPUNIT_ASSERT_THROW(validateAsyncNameResolverConfig(
                           AsyncNameResolverMan::RESOLVER_MULTI,
                           "dns.example.org"),
                       Exception);
}

void AsyncNameResolverTest::testValidateConfigRejectsMultiBadPlainPort()
{
  CPPUNIT_ASSERT_THROW(validateAsyncNameResolverConfig(
                           AsyncNameResolverMan::RESOLVER_MULTI,
                           "udp://1.1.1.1:bad"),
                       Exception);
}

void AsyncNameResolverTest::testConfigureAcceptsDotIpServers()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "1.1.1.1");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureAcceptsDotDomainServer()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "dns.example.org");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureRejectsDotEmptyServerList()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}

void AsyncNameResolverTest::testPlainBootstrapFactoryUsesConfiguredPlainServers()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncNameResolverTest_testPlainBootstrapFactoryUsesConfiguredPlainServers.log";
  ScopedNetworkLog log(logPath);
  auto config =
      parseAsyncDnsMultiServerConfigList("udp://1.1.1.1,tcp://1.0.0.1");
  auto factory = createPlainBootstrapResolverFactory(config);

  auto resolver = factory(AF_INET);
  resolver->resolve("dns.example.org");

  CPPUNIT_ASSERT_EQUAL(AF_INET, resolver->getFamily());
  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT_EQUAL(
      (size_t)1,
      countSubstring(logs, "DNS: multi plain bootstrap window activated"));
  CPPUNIT_ASSERT(logs.find("multi plain bootstrap window activated "
                           "candidate=1 active_limit=1") !=
                 std::string::npos);
}

void AsyncNameResolverTest::
    testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled()
{
  std::vector<std::pair<std::string, std::string>> configs;
  configs.push_back({V_DOT, ""});
  configs.push_back({V_DOH, ""});
  configs.push_back({V_MULTI, "dns.example.org"});

  for (const auto& config : configs) {
    Option option;
    option.put(PREF_ASYNC_DNS, A2_V_FALSE);
    option.put(PREF_ASYNC_DNS_MODE, config.first);
    option.put(PREF_ASYNC_DNS_SERVER, config.second);
    AsyncNameResolverMan resolverMan;

    configureAsyncNameResolverMan(&resolverMan, &option);
  }
}

void AsyncNameResolverTest::testConfigureAcceptsDohIpServers()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "https://1.1.1.1/dns-query");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureAcceptsDohDomainServer()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureRejectsDohEmptyServerList()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}

void AsyncNameResolverTest::testConfigureAcceptsMultiServers()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER,
             "udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,"
             "https://dns.example.org/dns-query");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureRejectsMultiPlainDomainServer()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER, "dns.example.org");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}
#endif // ENABLE_SSL

} // namespace aria2
