#include "AsyncServiceBindingResolver.h"

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class AsyncServiceBindingResolverTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AsyncServiceBindingResolverTest);
  CPPUNIT_TEST(testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort);
  CPPUNIT_TEST(testCreateHttpsServiceBindingQueryNameUsesPortPrefix);
  CPPUNIT_TEST_SUITE_END();

public:
  void testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort();
  void testCreateHttpsServiceBindingQueryNameUsesPortPrefix();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncServiceBindingResolverTest);

void AsyncServiceBindingResolverTest::
    testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort()
{
  CPPUNIT_ASSERT_EQUAL(
      std::string("www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 443));
  CPPUNIT_ASSERT_EQUAL(
      std::string("www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 0));
}

void AsyncServiceBindingResolverTest::
    testCreateHttpsServiceBindingQueryNameUsesPortPrefix()
{
  CPPUNIT_ASSERT_EQUAL(
      std::string("_8443._https.www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 8443));
}

} // namespace aria2
