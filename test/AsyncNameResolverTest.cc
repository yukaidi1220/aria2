#include "AsyncNameResolver.h"
#ifdef ENABLE_SSL
#  include "AsyncDohNameResolver.h"
#  include "AsyncDotNameResolver.h"
#  include "PlainBootstrapResolver.h"
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
#include "Option.h"
#include "prefs.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"

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
  CPPUNIT_TEST(testStartAsyncMultiStartsSecureBackendsBeforePlainFallback);
  CPPUNIT_TEST(testStartAsyncMultiFallsBackToExplicitPlainThenSystem);
  CPPUNIT_TEST(testValidateConfigAcceptsDotIpServers);
  CPPUNIT_TEST(testValidateConfigAcceptsDotDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDotEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsDohIpServers);
  CPPUNIT_TEST(testValidateConfigAcceptsDohDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDohEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsMultiServers);
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
  void testStartAsyncMultiStartsSecureBackendsBeforePlainFallback();
  void testStartAsyncMultiFallsBackToExplicitPlainThenSystem();
  void testValidateConfigAcceptsDotIpServers();
  void testValidateConfigAcceptsDotDomainServer();
  void testValidateConfigRejectsDotEmptyServerList();
  void testValidateConfigAcceptsDohIpServers();
  void testValidateConfigAcceptsDohDomainServer();
  void testValidateConfigRejectsDohEmptyServerList();
  void testValidateConfigAcceptsMultiServers();
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
  CPPUNIT_ASSERT_EQUAL((size_t)2, resolvers.size());
  CPPUNIT_ASSERT(dynamic_cast<AsyncDotNameResolver*>(resolvers[0].get()));
  CPPUNIT_ASSERT(dynamic_cast<AsyncDohNameResolver*>(resolvers[1].get()));

  resolverMan.startAsync("example.org", nullptr, &command);

  auto status = resolverMan.getQueryStatus();
  CPPUNIT_ASSERT_EQUAL((size_t)2, countQueryStatusEntries(status));
  CPPUNIT_ASSERT(status.find("A=") != std::string::npos);
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
  auto config =
      parseAsyncDnsMultiServerConfigList("udp://1.1.1.1,tcp://1.0.0.1");
  auto factory = createPlainBootstrapResolverFactory(config);

  auto resolver = factory(AF_INET);

  CPPUNIT_ASSERT(dynamic_cast<PlainBootstrapResolver*>(resolver.get()));
}

void AsyncNameResolverTest::
    testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_FALSE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
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
