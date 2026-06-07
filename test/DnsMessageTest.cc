#include "DnsMessage.h"

#include <string>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "DlAbortEx.h"

namespace aria2 {

class DnsMessageTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(DnsMessageTest);
  CPPUNIT_TEST(testCreateAQuery);
  CPPUNIT_TEST(testCreateAAAAQueryWithTrailingDot);
  CPPUNIT_TEST(testCreateQueryRejectsBadName);
  CPPUNIT_TEST(testParseAResponse);
  CPPUNIT_TEST(testParseAAAAResponse);
  CPPUNIT_TEST(testParseCompressedCNAMEThenAResponse);
  CPPUNIT_TEST(testParseAcceptsUnorderedCNAMEChain);
  CPPUNIT_TEST(testParseIgnoresUnmatchedAnswerType);
  CPPUNIT_TEST(testParseIgnoresUnrelatedAnswerOwner);
  CPPUNIT_TEST(testParseRejectsBadResponse);
  CPPUNIT_TEST(testParseRejectsBadQuestion);
  CPPUNIT_TEST(testParseRejectsBadCNAMERecord);
  CPPUNIT_TEST(testParseRejectsBadCompressionPointer);
  CPPUNIT_TEST_SUITE_END();

public:
  void testCreateAQuery();
  void testCreateAAAAQueryWithTrailingDot();
  void testCreateQueryRejectsBadName();
  void testParseAResponse();
  void testParseAAAAResponse();
  void testParseCompressedCNAMEThenAResponse();
  void testParseAcceptsUnorderedCNAMEChain();
  void testParseIgnoresUnmatchedAnswerType();
  void testParseIgnoresUnrelatedAnswerOwner();
  void testParseRejectsBadResponse();
  void testParseRejectsBadQuestion();
  void testParseRejectsBadCNAMERecord();
  void testParseRejectsBadCompressionPointer();
};

CPPUNIT_TEST_SUITE_REGISTRATION(DnsMessageTest);

namespace {

void appendUint16(std::string& out, uint16_t value)
{
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

void appendUint32(std::string& out, uint32_t value)
{
  out.push_back(static_cast<char>((value >> 24) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

void appendName(std::string& out, const std::string& name)
{
  size_t pos = 0;
  while (pos < name.size()) {
    auto dot = name.find('.', pos);
    auto end = dot == std::string::npos ? name.size() : dot;
    auto labelLen = end - pos;
    out.push_back(static_cast<char>(labelLen));
    out.append(name, pos, labelLen);
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }
  out.push_back(0);
}

void appendQuestion(std::string& out, const std::string& name, uint16_t type)
{
  appendName(out, name);
  appendUint16(out, type);
  appendUint16(out, 1);
}

void appendCompressedName(std::string& out, uint16_t offset)
{
  appendUint16(out, 0xc000 | offset);
}

void appendHeader(std::string& out, uint16_t id, uint16_t flags,
                  uint16_t qdcount, uint16_t ancount)
{
  appendUint16(out, id);
  appendUint16(out, flags);
  appendUint16(out, qdcount);
  appendUint16(out, ancount);
  appendUint16(out, 0);
  appendUint16(out, 0);
}

void appendAAnswer(std::string& out, uint16_t nameOffset, uint32_t ttl,
                   unsigned char a, unsigned char b, unsigned char c,
                   unsigned char d)
{
  appendCompressedName(out, nameOffset);
  appendUint16(out, dns::TYPE_A);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, 4);
  out.push_back(static_cast<char>(a));
  out.push_back(static_cast<char>(b));
  out.push_back(static_cast<char>(c));
  out.push_back(static_cast<char>(d));
}

void appendAAnswer(std::string& out, const std::string& name, uint32_t ttl,
                   unsigned char a, unsigned char b, unsigned char c,
                   unsigned char d)
{
  appendName(out, name);
  appendUint16(out, dns::TYPE_A);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, 4);
  out.push_back(static_cast<char>(a));
  out.push_back(static_cast<char>(b));
  out.push_back(static_cast<char>(c));
  out.push_back(static_cast<char>(d));
}

void appendAAAAAnswer(std::string& out, uint16_t nameOffset, uint32_t ttl,
                      const unsigned char addr[16])
{
  appendCompressedName(out, nameOffset);
  appendUint16(out, dns::TYPE_AAAA);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, 16);
  out.append(reinterpret_cast<const char*>(addr), 16);
}

void appendCNAMEAnswer(std::string& out, uint16_t nameOffset, uint32_t ttl,
                       const std::string& cname)
{
  std::string encoded;
  appendName(encoded, cname);

  appendCompressedName(out, nameOffset);
  appendUint16(out, 5);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, static_cast<uint16_t>(encoded.size()));
  out.append(encoded);
}

void appendCNAMEAnswer(std::string& out, const std::string& name, uint32_t ttl,
                       const std::string& cname)
{
  std::string encoded;
  appendName(encoded, cname);

  appendName(out, name);
  appendUint16(out, 5);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, static_cast<uint16_t>(encoded.size()));
  out.append(encoded);
}

std::vector<std::string> parse(const std::string& msg, uint16_t id,
                               dns::QueryType qtype,
                               const std::string& hostname =
                                   "www.example.com")
{
  return dns::parseResponse(
      reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), id,
      hostname, qtype);
}

std::string createResponseHeader(uint16_t id, uint16_t flags, uint16_t qtype,
                                 uint16_t ancount)
{
  std::string msg;
  appendHeader(msg, id, flags, 1, ancount);
  appendQuestion(msg, "www.example.com", qtype);
  return msg;
}

} // namespace

void DnsMessageTest::testCreateAQuery()
{
  std::string expected;
  appendHeader(expected, 0x1234, 0x0100, 1, 0);
  appendQuestion(expected, "www.example.com", dns::TYPE_A);

  CPPUNIT_ASSERT_EQUAL(expected,
                       dns::createQuery(0x1234, "www.example.com",
                                        dns::TYPE_A));
}

void DnsMessageTest::testCreateAAAAQueryWithTrailingDot()
{
  std::string expected;
  appendHeader(expected, 0x2345, 0x0100, 1, 0);
  appendQuestion(expected, "www.example.com", dns::TYPE_AAAA);

  CPPUNIT_ASSERT_EQUAL(expected,
                       dns::createQuery(0x2345, "www.example.com.",
                                        dns::TYPE_AAAA));
}

void DnsMessageTest::testCreateQueryRejectsBadName()
{
  CPPUNIT_ASSERT_THROW(dns::createQuery(1, "", dns::TYPE_A), DlAbortEx);
  CPPUNIT_ASSERT_THROW(dns::createQuery(1, ".", dns::TYPE_A), DlAbortEx);
  CPPUNIT_ASSERT_THROW(dns::createQuery(1, "www..example", dns::TYPE_A),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(dns::createQuery(1, std::string(64, 'a') + ".example",
                                        dns::TYPE_A),
                       DlAbortEx);

  std::string tooLong;
  for (size_t i = 0; i < 128; ++i) {
    if (!tooLong.empty()) {
      tooLong += '.';
    }
    tooLong += 'a';
  }
  CPPUNIT_ASSERT_THROW(dns::createQuery(1, tooLong, dns::TYPE_A), DlAbortEx);
}

void DnsMessageTest::testParseAResponse()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendAAnswer(msg, 12, 60, 198, 51, 100, 7);

  auto result = parse(msg, 0x1234, dns::TYPE_A);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.7"), result[0]);
}

void DnsMessageTest::testParseAAAAResponse()
{
  static const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 1};
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_AAAA, 1);
  appendAAAAAnswer(msg, 12, 60, addr);

  auto result = parse(msg, 0x1234, dns::TYPE_AAAA);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), result[0]);
}

void DnsMessageTest::testParseCompressedCNAMEThenAResponse()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 2);
  appendCNAMEAnswer(msg, 12, 60, "alias.example.com");
  appendAAnswer(msg, "alias.example.com", 60, 203, 0, 113, 9);

  auto result = parse(msg, 0x1234, dns::TYPE_A);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.9"), result[0]);
}

void DnsMessageTest::testParseAcceptsUnorderedCNAMEChain()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 3);
  appendAAnswer(msg, "cdn.example.com", 60, 192, 0, 2, 45);
  appendCNAMEAnswer(msg, 12, 60, "alias.example.com");
  appendCNAMEAnswer(msg, "alias.example.com", 60, "cdn.example.com");

  auto result = parse(msg, 0x1234, dns::TYPE_A);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.45"), result[0]);
}

void DnsMessageTest::testParseIgnoresUnmatchedAnswerType()
{
  static const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 1};
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendAAAAAnswer(msg, 12, 60, addr);

  auto result = parse(msg, 0x1234, dns::TYPE_A);
  CPPUNIT_ASSERT(result.empty());
}

void DnsMessageTest::testParseIgnoresUnrelatedAnswerOwner()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendAAnswer(msg, "other.example.com", 60, 198, 51, 100, 7);

  auto result = parse(msg, 0x1234, dns::TYPE_A);
  CPPUNIT_ASSERT(result.empty());
}

void DnsMessageTest::testParseRejectsBadResponse()
{
  std::string tooShort(11, '\0');
  CPPUNIT_ASSERT_THROW(parse(tooShort, 0x1234, dns::TYPE_A), DlAbortEx);

  auto good = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendAAnswer(good, 12, 60, 198, 51, 100, 7);
  CPPUNIT_ASSERT_THROW(parse(good, 0x4321, dns::TYPE_A), DlAbortEx);
  CPPUNIT_ASSERT_THROW(parse(good, 0x1234, dns::TYPE_A, "other.example.com"),
                       DlAbortEx);

  auto query = createResponseHeader(0x1234, 0x0100, dns::TYPE_A, 1);
  appendAAnswer(query, 12, 60, 198, 51, 100, 7);
  CPPUNIT_ASSERT_THROW(parse(query, 0x1234, dns::TYPE_A), DlAbortEx);

  auto nonStandardOpcode = createResponseHeader(0x1234, 0x8980, dns::TYPE_A, 1);
  appendAAnswer(nonStandardOpcode, 12, 60, 198, 51, 100, 7);
  CPPUNIT_ASSERT_THROW(parse(nonStandardOpcode, 0x1234, dns::TYPE_A),
                       DlAbortEx);

  auto truncated = createResponseHeader(0x1234, 0x8380, dns::TYPE_A, 1);
  appendAAnswer(truncated, 12, 60, 198, 51, 100, 7);
  CPPUNIT_ASSERT_THROW(parse(truncated, 0x1234, dns::TYPE_A), DlAbortEx);

  auto nxdomain = createResponseHeader(0x1234, 0x8183, dns::TYPE_A, 0);
  CPPUNIT_ASSERT_THROW(parse(nxdomain, 0x1234, dns::TYPE_A), DlAbortEx);

  auto clipped = good;
  clipped.resize(clipped.size() - 1);
  CPPUNIT_ASSERT_THROW(parse(clipped, 0x1234, dns::TYPE_A), DlAbortEx);
}

void DnsMessageTest::testParseRejectsBadQuestion()
{
  std::string noQuestion;
  appendHeader(noQuestion, 0x1234, 0x8180, 0, 0);
  CPPUNIT_ASSERT_THROW(parse(noQuestion, 0x1234, dns::TYPE_A), DlAbortEx);

  auto wrongType = createResponseHeader(0x1234, 0x8180, dns::TYPE_AAAA, 0);
  CPPUNIT_ASSERT_THROW(parse(wrongType, 0x1234, dns::TYPE_A), DlAbortEx);

  std::string wrongClass;
  appendHeader(wrongClass, 0x1234, 0x8180, 1, 0);
  appendName(wrongClass, "www.example.com");
  appendUint16(wrongClass, dns::TYPE_A);
  appendUint16(wrongClass, 3);
  CPPUNIT_ASSERT_THROW(parse(wrongClass, 0x1234, dns::TYPE_A), DlAbortEx);
}

void DnsMessageTest::testParseRejectsBadCNAMERecord()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendCompressedName(msg, 12);
  appendUint16(msg, 5);
  appendUint16(msg, 1);
  appendUint32(msg, 60);
  appendUint16(msg, 3);
  msg.append("\x05xx", 3);

  CPPUNIT_ASSERT_THROW(parse(msg, 0x1234, dns::TYPE_A), DlAbortEx);
}

void DnsMessageTest::testParseRejectsBadCompressionPointer()
{
  auto selfPointer = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendCompressedName(selfPointer, static_cast<uint16_t>(selfPointer.size()));
  appendUint16(selfPointer, dns::TYPE_A);
  appendUint16(selfPointer, 1);
  appendUint32(selfPointer, 60);
  appendUint16(selfPointer, 4);
  selfPointer.append("\x01\x02\x03\x04", 4);
  CPPUNIT_ASSERT_THROW(parse(selfPointer, 0x1234, dns::TYPE_A), DlAbortEx);

  auto outOfRange = createResponseHeader(0x1234, 0x8180, dns::TYPE_A, 1);
  appendCompressedName(outOfRange, 0x3fff);
  appendUint16(outOfRange, dns::TYPE_A);
  appendUint16(outOfRange, 1);
  appendUint32(outOfRange, 60);
  appendUint16(outOfRange, 4);
  outOfRange.append("\x01\x02\x03\x04", 4);
  CPPUNIT_ASSERT_THROW(parse(outOfRange, 0x1234, dns::TYPE_A), DlAbortEx);

  std::string overlongName;
  appendHeader(overlongName, 0x1234, 0x8180, 1, 0);
  for (size_t i = 0; i < 128; ++i) {
    overlongName.push_back(1);
    overlongName.push_back('a');
  }
  overlongName.push_back(0);
  appendUint16(overlongName, dns::TYPE_A);
  appendUint16(overlongName, 1);
  CPPUNIT_ASSERT_THROW(parse(overlongName, 0x1234, dns::TYPE_A), DlAbortEx);
}

} // namespace aria2
