#include "HostMapping.h"

#include <string>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "DlAbortEx.h"
#include "Option.h"
#include "prefs.h"

namespace aria2 {

class HostMappingTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HostMappingTest);
  CPPUNIT_TEST(testUndefinedOption);
  CPPUNIT_TEST(testHostnameToIPv4);
  CPPUNIT_TEST(testHostnameToIPv6);
  CPPUNIT_TEST(testIPv4ToHostname);
  CPPUNIT_TEST(testIPv6ToHostname);
  CPPUNIT_TEST(testBracketedRequestIPv6ToHostname);
  CPPUNIT_TEST(testCaseInsensitiveHostname);
  CPPUNIT_TEST(testMultipleAddresses);
  CPPUNIT_TEST(testMalformedMapping);
  CPPUNIT_TEST_SUITE_END();

public:
  void testUndefinedOption();
  void testHostnameToIPv4();
  void testHostnameToIPv6();
  void testIPv4ToHostname();
  void testIPv6ToHostname();
  void testBracketedRequestIPv6ToHostname();
  void testCaseInsensitiveHostname();
  void testMultipleAddresses();
  void testMalformedMapping();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HostMappingTest);

void HostMappingTest::testUndefinedOption()
{
  Option option;
  auto addrs = getMappedAddresses("origin.example", &option);
  CPPUNIT_ASSERT(addrs.empty());
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"),
                       getLogicalHostForRequest("origin.example", &option));
}

void HostMappingTest::testHostnameToIPv4()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING, "origin.example:198.18.0.18");

  auto addrs = getMappedAddresses("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL((size_t)1, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.18.0.18"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"),
                       getLogicalHostForRequest("origin.example", &option));
}

void HostMappingTest::testHostnameToIPv6()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING, "origin.example:[2001:db8::1]");

  auto addrs = getMappedAddresses("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL((size_t)1, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[0]);
}

void HostMappingTest::testIPv4ToHostname()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING, "198.18.0.18:origin.example");

  auto addrs = getMappedAddresses("198.18.0.18", &option);
  CPPUNIT_ASSERT(addrs.empty());
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"),
                       getLogicalHostForRequest("198.18.0.18", &option));
}

void HostMappingTest::testIPv6ToHostname()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING, "[2001:db8::1]:origin.example");

  auto addrs = getMappedAddresses("2001:db8::1", &option);
  CPPUNIT_ASSERT(addrs.empty());
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"),
                       getLogicalHostForRequest("2001:db8::1", &option));
}

void HostMappingTest::testBracketedRequestIPv6ToHostname()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING, "[2001:db8::1]:origin.example");

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"),
                       getLogicalHostForRequest("[2001:db8::1]", &option));
}

void HostMappingTest::testCaseInsensitiveHostname()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING,
             "Origin.Example:198.18.0.18,198.18.0.19:Front.Example");

  auto addrs = getMappedAddresses("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL((size_t)1, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.18.0.18"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("front.example"),
                       getLogicalHostForRequest("198.18.0.19", &option));
}

void HostMappingTest::testMultipleAddresses()
{
  Option option;
  option.put(PREF_HOSTS_MAPPING,
             "origin.example:198.18.0.18,origin.example:198.18.0.19");

  auto addrs = getMappedAddresses("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL((size_t)2, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.18.0.18"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("198.18.0.19"), addrs[1]);
}

void HostMappingTest::testMalformedMapping()
{
  Option option;

  option.put(PREF_HOSTS_MAPPING,
             "origin.example:198.18.0.18,,other:198.18.0.19");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, ":198.18.0.18");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, "origin.example:");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, "origin.example:front.example");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, "198.18.0.18:198.18.0.19");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, "origin.example:2001:db8::1");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_HOSTS_MAPPING, "[2001:db8::1:origin.example");
  CPPUNIT_ASSERT_THROW(getMappedAddresses("2001:db8::1", &option),
                       DlAbortEx);
}

} // namespace aria2
