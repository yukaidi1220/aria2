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
#include "Option.h"
#include "prefs.h"
#include "SocketCore.h"

namespace aria2 {

class AsyncNameResolverTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AsyncNameResolverTest);
  CPPUNIT_TEST(testGetQueryStatusBeforeStart);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv6SucceedsAndIPv4Fails);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv6SucceedsAndIPv4IsPending);
  CPPUNIT_TEST(testGetStatusSucceedsWhenIPv4SucceedsAndIPv6IsPending);
  CPPUNIT_TEST(testValidateConfigLeavesCaresServersUnchanged);
#ifdef ENABLE_SSL
  CPPUNIT_TEST(testCreateDotResolver);
  CPPUNIT_TEST(testCreateDotResolverRejectsDomainServer);
  CPPUNIT_TEST(testCreateDotResolverRejectsEmptyServerList);
  CPPUNIT_TEST(testCreateDohResolver);
  CPPUNIT_TEST(testCreateDohResolverRejectsDomainServer);
  CPPUNIT_TEST(testCreateDohResolverRejectsEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsDotIpServers);
  CPPUNIT_TEST(testValidateConfigRejectsDotDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDotEmptyServerList);
  CPPUNIT_TEST(testValidateConfigAcceptsDohIpServers);
  CPPUNIT_TEST(testValidateConfigRejectsDohDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDohEmptyServerList);
  CPPUNIT_TEST(testConfigureAcceptsDotIpServers);
  CPPUNIT_TEST(testConfigureRejectsDotDomainServer);
  CPPUNIT_TEST(testConfigureRejectsDotEmptyServerList);
  CPPUNIT_TEST(testConfigureAcceptsDohIpServers);
  CPPUNIT_TEST(testConfigureRejectsDohDomainServer);
  CPPUNIT_TEST(testConfigureRejectsDohEmptyServerList);
#endif // ENABLE_SSL
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}

  void tearDown() {}

  void testGetQueryStatusBeforeStart();
  void testGetStatusSucceedsWhenIPv6SucceedsAndIPv4Fails();
  void testGetStatusSucceedsWhenIPv6SucceedsAndIPv4IsPending();
  void testGetStatusSucceedsWhenIPv4SucceedsAndIPv6IsPending();
  void testValidateConfigLeavesCaresServersUnchanged();
#ifdef ENABLE_SSL
  void testCreateDotResolver();
  void testCreateDotResolverRejectsDomainServer();
  void testCreateDotResolverRejectsEmptyServerList();
  void testCreateDohResolver();
  void testCreateDohResolverRejectsDomainServer();
  void testCreateDohResolverRejectsEmptyServerList();
  void testValidateConfigAcceptsDotIpServers();
  void testValidateConfigRejectsDotDomainServer();
  void testValidateConfigRejectsDotEmptyServerList();
  void testValidateConfigAcceptsDohIpServers();
  void testValidateConfigRejectsDohDomainServer();
  void testValidateConfigRejectsDohEmptyServerList();
  void testConfigureAcceptsDotIpServers();
  void testConfigureRejectsDotDomainServer();
  void testConfigureRejectsDotEmptyServerList();
  void testConfigureAcceptsDohIpServers();
  void testConfigureRejectsDohDomainServer();
  void testConfigureRejectsDohEmptyServerList();
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

class MockCommand : public Command {
public:
  explicit MockCommand(cuid_t cuid) : Command(cuid) {}

  bool execute() CXX11_OVERRIDE { return true; }
};

} // namespace

void AsyncNameResolverTest::testGetQueryStatusBeforeStart()
{
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_EQUAL(std::string(), resolverMan.getQueryStatus());
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

void AsyncNameResolverTest::testValidateConfigLeavesCaresServersUnchanged()
{
  validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_CARES,
                                  "dns.example.org");
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

void AsyncNameResolverTest::testCreateDotResolverRejectsDomainServer()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOT);
  resolverMan.setServers("dns.example.org");

  CPPUNIT_ASSERT_THROW(resolverMan.createResolver(AF_INET), Exception);
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

void AsyncNameResolverTest::testCreateDohResolverRejectsDomainServer()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);
  resolverMan.setServers("https://dns.example.org/dns-query");

  CPPUNIT_ASSERT_THROW(resolverMan.createResolver(AF_INET), Exception);
}

void AsyncNameResolverTest::testCreateDohResolverRejectsEmptyServerList()
{
  AsyncNameResolverMan resolverMan;
  resolverMan.setResolverMode(AsyncNameResolverMan::RESOLVER_DOH);

  CPPUNIT_ASSERT_THROW(resolverMan.createResolver(AF_INET), Exception);
}

void AsyncNameResolverTest::testValidateConfigAcceptsDotIpServers()
{
  validateAsyncNameResolverConfig(
      AsyncNameResolverMan::RESOLVER_DOT,
      "1.1.1.1,[2606:4700:4700::1111]:853");
}

void AsyncNameResolverTest::testValidateConfigRejectsDotDomainServer()
{
  CPPUNIT_ASSERT_THROW(
      validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOT,
                                      "dns.example.org"),
      Exception);
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

void AsyncNameResolverTest::testValidateConfigRejectsDohDomainServer()
{
  CPPUNIT_ASSERT_THROW(
      validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOH,
                                      "https://dns.example.org/dns-query"),
      Exception);
}

void AsyncNameResolverTest::testValidateConfigRejectsDohEmptyServerList()
{
  CPPUNIT_ASSERT_THROW(
      validateAsyncNameResolverConfig(AsyncNameResolverMan::RESOLVER_DOH, ""),
      Exception);
}

void AsyncNameResolverTest::testConfigureAcceptsDotIpServers()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "1.1.1.1");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureRejectsDotDomainServer()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "dns.example.org");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}

void AsyncNameResolverTest::testConfigureRejectsDotEmptyServerList()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}

void AsyncNameResolverTest::testConfigureAcceptsDohIpServers()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "https://1.1.1.1/dns-query");
  AsyncNameResolverMan resolverMan;

  configureAsyncNameResolverMan(&resolverMan, &option);
}

void AsyncNameResolverTest::testConfigureRejectsDohDomainServer()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}

void AsyncNameResolverTest::testConfigureRejectsDohEmptyServerList()
{
  Option option;
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "");
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_THROW(configureAsyncNameResolverMan(&resolverMan, &option),
                       Exception);
}
#endif // ENABLE_SSL

} // namespace aria2
