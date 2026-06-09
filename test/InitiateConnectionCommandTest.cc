#include "InitiateConnectionCommand.h"

#include <string>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "A2STR.h"
#include "Option.h"
#include "prefs.h"

namespace aria2 {

class InitiateConnectionCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(InitiateConnectionCommandTest);
  CPPUNIT_TEST(testSelectBackupIPAddressChoosesIPv6ForIPv4Main);
  CPPUNIT_TEST(testSelectBackupIPAddressChoosesIPv4ForIPv6Main);
  CPPUNIT_TEST(testSelectBackupIPAddressReturnsEmptyWithoutOppositeFamily);
  CPPUNIT_TEST(testSelectBackupIPAddressReturnsEmptyForHostname);
  CPPUNIT_TEST(testSelectBackupIPAddressUsesResolvedOrder);
#ifdef ENABLE_ASYNC_DNS
  CPPUNIT_TEST(testGetBackupConnectionDelayKeepsDefaultWithoutAsyncDns);
  CPPUNIT_TEST(testGetBackupConnectionDelayUsesZeroWithAsyncDns);
  CPPUNIT_TEST(testGetBackupConnectionDelayKeepsDefaultWhenIPv6Disabled);
#endif // ENABLE_ASYNC_DNS
  CPPUNIT_TEST_SUITE_END();

public:
  void testSelectBackupIPAddressChoosesIPv6ForIPv4Main();
  void testSelectBackupIPAddressChoosesIPv4ForIPv6Main();
  void testSelectBackupIPAddressReturnsEmptyWithoutOppositeFamily();
  void testSelectBackupIPAddressReturnsEmptyForHostname();
  void testSelectBackupIPAddressUsesResolvedOrder();
#ifdef ENABLE_ASYNC_DNS
  void testGetBackupConnectionDelayKeepsDefaultWithoutAsyncDns();
  void testGetBackupConnectionDelayUsesZeroWithAsyncDns();
  void testGetBackupConnectionDelayKeepsDefaultWhenIPv6Disabled();
#endif // ENABLE_ASYNC_DNS
};

CPPUNIT_TEST_SUITE_REGISTRATION(InitiateConnectionCommandTest);

void InitiateConnectionCommandTest::
    testSelectBackupIPAddressChoosesIPv6ForIPv4Main()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.2");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectBackupIPAddress(addrs, "192.0.2.10"));
}

void InitiateConnectionCommandTest::
    testSelectBackupIPAddressChoosesIPv4ForIPv6Main()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::2");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectBackupIPAddress(addrs, "2001:db8::10"));
}

void InitiateConnectionCommandTest::
    testSelectBackupIPAddressReturnsEmptyWithoutOppositeFamily()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("192.0.2.2");

  CPPUNIT_ASSERT_EQUAL(std::string(),
                       selectBackupIPAddress(addrs, "192.0.2.10"));
}

void InitiateConnectionCommandTest::
    testSelectBackupIPAddressReturnsEmptyForHostname()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  CPPUNIT_ASSERT_EQUAL(std::string(),
                       selectBackupIPAddress(addrs, "example.org"));
}

void InitiateConnectionCommandTest::testSelectBackupIPAddressUsesResolvedOrder()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::2");
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::2"),
                       selectBackupIPAddress(addrs, "192.0.2.10"));
}

#ifdef ENABLE_ASYNC_DNS
void InitiateConnectionCommandTest::
    testGetBackupConnectionDelayKeepsDefaultWithoutAsyncDns()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_FALSE);
  option.put(PREF_DISABLE_IPV6, A2_V_FALSE);

  CPPUNIT_ASSERT_EQUAL((int64_t)300, getBackupConnectionDelay(&option).count());
}

void InitiateConnectionCommandTest::
    testGetBackupConnectionDelayUsesZeroWithAsyncDns()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_DISABLE_IPV6, A2_V_FALSE);

  CPPUNIT_ASSERT_EQUAL((int64_t)0, getBackupConnectionDelay(&option).count());
}

void InitiateConnectionCommandTest::
    testGetBackupConnectionDelayKeepsDefaultWhenIPv6Disabled()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_DISABLE_IPV6, A2_V_TRUE);

  CPPUNIT_ASSERT_EQUAL((int64_t)300, getBackupConnectionDelay(&option).count());
}
#endif // ENABLE_ASYNC_DNS

} // namespace aria2
