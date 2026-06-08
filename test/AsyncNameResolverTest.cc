#include "AsyncNameResolver.h"
#ifdef ENABLE_SSL
#  include "AsyncDohNameResolver.h"
#  include "AsyncDotNameResolver.h"
#endif // ENABLE_SSL
#include "AsyncNameResolverMan.h"

#include <cstring>

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"
#include "Option.h"
#include "prefs.h"
#include "SocketCore.h"

namespace aria2 {

class AsyncNameResolverTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AsyncNameResolverTest);
  CPPUNIT_TEST(testGetQueryStatusBeforeStart);
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

void AsyncNameResolverTest::testGetQueryStatusBeforeStart()
{
  AsyncNameResolverMan resolverMan;

  CPPUNIT_ASSERT_EQUAL(std::string(), resolverMan.getQueryStatus());
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
