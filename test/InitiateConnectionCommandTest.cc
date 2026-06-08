#include "InitiateConnectionCommand.h"

#include <string>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class InitiateConnectionCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(InitiateConnectionCommandTest);
  CPPUNIT_TEST(testSelectBackupIPAddressChoosesIPv6ForIPv4Main);
  CPPUNIT_TEST(testSelectBackupIPAddressChoosesIPv4ForIPv6Main);
  CPPUNIT_TEST(testSelectBackupIPAddressReturnsEmptyWithoutOppositeFamily);
  CPPUNIT_TEST(testSelectBackupIPAddressReturnsEmptyForHostname);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSelectBackupIPAddressChoosesIPv6ForIPv4Main();
  void testSelectBackupIPAddressChoosesIPv4ForIPv6Main();
  void testSelectBackupIPAddressReturnsEmptyWithoutOppositeFamily();
  void testSelectBackupIPAddressReturnsEmptyForHostname();
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

} // namespace aria2
