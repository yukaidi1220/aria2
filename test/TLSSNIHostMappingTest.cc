#include "TLSSNIHostMapping.h"

#include <string>

#include <cppunit/extensions/HelperMacros.h>

#include "DlAbortEx.h"
#include "Option.h"
#include "prefs.h"

namespace aria2 {

class TLSSNIHostMappingTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TLSSNIHostMappingTest);
  CPPUNIT_TEST(testUndefinedOption);
  CPPUNIT_TEST(testSingleHostCompatibility);
  CPPUNIT_TEST(testMappedHost);
  CPPUNIT_TEST(testMappedRedirectHost);
  CPPUNIT_TEST(testUnmatchedMapping);
  CPPUNIT_TEST(testMappedIPv4Target);
  CPPUNIT_TEST(testMappedIPv6Target);
  CPPUNIT_TEST(testCaseInsensitiveTarget);
  CPPUNIT_TEST(testFirstMatchingEntryWins);
  CPPUNIT_TEST(testMalformedMapping);
  CPPUNIT_TEST_SUITE_END();

public:
  void testUndefinedOption();
  void testSingleHostCompatibility();
  void testMappedHost();
  void testMappedRedirectHost();
  void testUnmatchedMapping();
  void testMappedIPv4Target();
  void testMappedIPv6Target();
  void testCaseInsensitiveTarget();
  void testFirstMatchingEntryWins();
  void testMalformedMapping();
};

CPPUNIT_TEST_SUITE_REGISTRATION(TLSSNIHostMappingTest);

void TLSSNIHostMappingTest::testUndefinedOption()
{
  Option option;
  auto config = getTLSSNIHostConfig("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), config.sniHost);
  CPPUNIT_ASSERT(!config.overridden);
}

void TLSSNIHostMappingTest::testSingleHostCompatibility()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST, "front.example");

  auto config = getTLSSNIHostConfig("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("front.example"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testMappedHost()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST,
             "origin.example:front.example,mirror.example:mirror-front.example");

  auto config = getTLSSNIHostConfig("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("front.example"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testMappedRedirectHost()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST,
             "silver.yukaidi.com:silver.yukaidi.com,"
             "skip.yukaidi.top:skip.yukaidi.top");

  auto first = getTLSSNIHostConfig("silver.yukaidi.com", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("silver.yukaidi.com"), first.sniHost);
  CPPUNIT_ASSERT(first.overridden);

  auto redirected = getTLSSNIHostConfig("skip.yukaidi.top", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("skip.yukaidi.top"), redirected.sniHost);
  CPPUNIT_ASSERT(redirected.overridden);
}

void TLSSNIHostMappingTest::testUnmatchedMapping()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST, "origin.example:front.example");

  auto config = getTLSSNIHostConfig("other.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("other.example"), config.sniHost);
  CPPUNIT_ASSERT(!config.overridden);
}

void TLSSNIHostMappingTest::testMappedIPv4Target()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST, "198.18.0.18:silver.yukaidi.com");

  auto config = getTLSSNIHostConfig("198.18.0.18", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("silver.yukaidi.com"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testMappedIPv6Target()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST, "[2001:db8::1]:front.example");

  auto config = getTLSSNIHostConfig("2001:db8::1", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("front.example"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testCaseInsensitiveTarget()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST, "Origin.Example:Front.Example");

  auto config = getTLSSNIHostConfig("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("Front.Example"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testFirstMatchingEntryWins()
{
  Option option;
  option.put(PREF_TLS_SNI_HOST,
             "origin.example:first.example,origin.example:second.example");

  auto config = getTLSSNIHostConfig("origin.example", &option);
  CPPUNIT_ASSERT_EQUAL(std::string("first.example"), config.sniHost);
  CPPUNIT_ASSERT(config.overridden);
}

void TLSSNIHostMappingTest::testMalformedMapping()
{
  Option option;

  option.put(PREF_TLS_SNI_HOST, "origin.example:front.example,,other:front");
  CPPUNIT_ASSERT_THROW(getTLSSNIHostConfig("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_TLS_SNI_HOST, ":front.example");
  CPPUNIT_ASSERT_THROW(getTLSSNIHostConfig("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_TLS_SNI_HOST, "origin.example:");
  CPPUNIT_ASSERT_THROW(getTLSSNIHostConfig("origin.example", &option),
                       DlAbortEx);

  option.put(PREF_TLS_SNI_HOST, "2001:db8::1:front.example");
  CPPUNIT_ASSERT_THROW(getTLSSNIHostConfig("2001:db8::1", &option),
                       DlAbortEx);

  option.put(PREF_TLS_SNI_HOST, "[2001:db8::1:front.example");
  CPPUNIT_ASSERT_THROW(getTLSSNIHostConfig("2001:db8::1", &option),
                       DlAbortEx);
}

} // namespace aria2
