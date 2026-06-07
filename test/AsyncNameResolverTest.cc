#include "AsyncNameResolver.h"
#ifdef ENABLE_SSL
#  include "AsyncDotNameResolver.h"
#endif // ENABLE_SSL
#include "AsyncNameResolverMan.h"

#include <cstring>

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"
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
  CPPUNIT_TEST(testValidateConfigAcceptsDotIpServers);
  CPPUNIT_TEST(testValidateConfigRejectsDotDomainServer);
  CPPUNIT_TEST(testValidateConfigRejectsDotEmptyServerList);
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
  void testValidateConfigAcceptsDotIpServers();
  void testValidateConfigRejectsDotDomainServer();
  void testValidateConfigRejectsDotEmptyServerList();
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
#endif // ENABLE_SSL

} // namespace aria2
