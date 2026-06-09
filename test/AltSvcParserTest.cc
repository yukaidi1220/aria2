#include "AltSvcParser.h"

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class AltSvcParserTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AltSvcParserTest);
  CPPUNIT_TEST(testParseH3AuthorityForms);
  CPPUNIT_TEST(testParseH3DraftProtocol);
  CPPUNIT_TEST(testParseIPv6Authority);
  CPPUNIT_TEST(testIgnoreNonH3AndInvalidItems);
  CPPUNIT_TEST(testIgnoreClear);
  CPPUNIT_TEST(testClearOverridesOtherEntries);
  CPPUNIT_TEST(testQuotedSeparatorsDoNotSplitItems);
  CPPUNIT_TEST(testBadOptionalParamsDoNotRejectItem);
  CPPUNIT_TEST_SUITE_END();

public:
  void testParseH3AuthorityForms();
  void testParseH3DraftProtocol();
  void testParseIPv6Authority();
  void testIgnoreNonH3AndInvalidItems();
  void testIgnoreClear();
  void testClearOverridesOtherEntries();
  void testQuotedSeparatorsDoNotSplitItems();
  void testBadOptionalParamsDoNotRejectItem();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AltSvcParserTest);

void AltSvcParserTest::testParseH3AuthorityForms()
{
  auto header =
      parseAltSvcHeader("h2=\":443\"; ma=30, h3=\":443\"; ma=60; persist=1, "
                        "h3=\"example.com:8443\"; persist=0");
  auto& entries = header.entries;

  CPPUNIT_ASSERT(!header.clear);
  CPPUNIT_ASSERT_EQUAL((size_t)2, entries.size());

  CPPUNIT_ASSERT_EQUAL(std::string("h3"), entries[0].protocolId);
  CPPUNIT_ASSERT_EQUAL(std::string(""), entries[0].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, entries[0].port);
  CPPUNIT_ASSERT_EQUAL((uint64_t)60, entries[0].maxAge);
  CPPUNIT_ASSERT(entries[0].persist);

  CPPUNIT_ASSERT_EQUAL(std::string("h3"), entries[1].protocolId);
  CPPUNIT_ASSERT_EQUAL(std::string("example.com"), entries[1].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, entries[1].port);
  CPPUNIT_ASSERT_EQUAL((uint64_t)86400, entries[1].maxAge);
  CPPUNIT_ASSERT(!entries[1].persist);
}

void AltSvcParserTest::testParseH3DraftProtocol()
{
  auto header = parseAltSvcHeader("h3-29=\":9443\", h3=\":9443\"; ma=\"120\"");
  auto& entries = header.entries;

  CPPUNIT_ASSERT_EQUAL((size_t)1, entries.size());
  CPPUNIT_ASSERT_EQUAL(std::string("h3"), entries[0].protocolId);
  CPPUNIT_ASSERT_EQUAL(std::string(""), entries[0].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)9443, entries[0].port);
  CPPUNIT_ASSERT_EQUAL((uint64_t)120, entries[0].maxAge);
  CPPUNIT_ASSERT(!entries[0].persist);
}

void AltSvcParserTest::testParseIPv6Authority()
{
  auto header = parseAltSvcHeader("h3=\"[2001:db8::1]:9443\"; ma=120");
  auto& entries = header.entries;

  CPPUNIT_ASSERT_EQUAL((size_t)1, entries.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), entries[0].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)9443, entries[0].port);
}

void AltSvcParserTest::testIgnoreNonH3AndInvalidItems()
{
  auto header =
      parseAltSvcHeader("h2=\":443\", h3=\":0\", h3=\"example.com\", "
                        "h3=\"example.com:bad\", h3-29=\":65536\", "
                        "h3-foo=\":444\", h3=\"2001:db8::1:443\"");

  CPPUNIT_ASSERT(header.entries.empty());
}

void AltSvcParserTest::testIgnoreClear()
{
  auto header = parseAltSvcHeader("clear");

  CPPUNIT_ASSERT(header.clear);
  CPPUNIT_ASSERT(header.entries.empty());
}

void AltSvcParserTest::testClearOverridesOtherEntries()
{
  auto header = parseAltSvcHeader("h3=\":443\", clear, h3=\":8443\"");

  CPPUNIT_ASSERT(header.clear);
  CPPUNIT_ASSERT(header.entries.empty());
}

void AltSvcParserTest::testQuotedSeparatorsDoNotSplitItems()
{
  auto header =
      parseAltSvcHeader("h3=\"alt.example:443\"; note=\"a,b;c\", h3=\":8443\"");
  auto& entries = header.entries;

  CPPUNIT_ASSERT_EQUAL((size_t)2, entries.size());
  CPPUNIT_ASSERT_EQUAL(std::string("alt.example"), entries[0].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, entries[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string(""), entries[1].host);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, entries[1].port);
}

void AltSvcParserTest::testBadOptionalParamsDoNotRejectItem()
{
  auto header =
      parseAltSvcHeader("h3=\":443\"; ma=oops; persist; broken param");
  auto& entries = header.entries;

  CPPUNIT_ASSERT_EQUAL((size_t)1, entries.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, entries[0].port);
  CPPUNIT_ASSERT_EQUAL((uint64_t)86400, entries[0].maxAge);
  CPPUNIT_ASSERT(!entries[0].persist);
}

} // namespace aria2
