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
  CPPUNIT_TEST(testCreateSVCBQuery);
  CPPUNIT_TEST(testCreateHTTPSQuery);
  CPPUNIT_TEST(testCreateQueryRejectsBadName);
  CPPUNIT_TEST(testParseAResponse);
  CPPUNIT_TEST(testParseAAAAResponse);
  CPPUNIT_TEST(testParseBasicSVCBResponse);
  CPPUNIT_TEST(testParseBasicHTTPSResponse);
  CPPUNIT_TEST(testParseHTTPSResponseParams);
  CPPUNIT_TEST(testParseHTTPSResponseUnknownParam);
  CPPUNIT_TEST(testParseHTTPSResponseCompressedTargetName);
  CPPUNIT_TEST(testParseHTTPSResponseRejectsBadParamKeyOrder);
  CPPUNIT_TEST(testParseHTTPSResponseRejectsBadParamLength);
  CPPUNIT_TEST(testParseHTTPSResponseRejectsNoDefaultAlpnWithoutAlpn);
  CPPUNIT_TEST(testParseHTTPSResponseMandatoryParam);
  CPPUNIT_TEST(testParseHTTPSResponseRejectsBadMandatoryParam);
  CPPUNIT_TEST(testParseHTTPSResponseIgnoresUnknownMandatoryParam);
  CPPUNIT_TEST(testParseHTTPSResponseRootTargetName);
  CPPUNIT_TEST(testParseHTTPSResponseAliasModeIgnoresParams);
  CPPUNIT_TEST(testParseHTTPSResponseAcceptsCNAMEChain);
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
  void testCreateSVCBQuery();
  void testCreateHTTPSQuery();
  void testCreateQueryRejectsBadName();
  void testParseAResponse();
  void testParseAAAAResponse();
  void testParseBasicSVCBResponse();
  void testParseBasicHTTPSResponse();
  void testParseHTTPSResponseParams();
  void testParseHTTPSResponseUnknownParam();
  void testParseHTTPSResponseCompressedTargetName();
  void testParseHTTPSResponseRejectsBadParamKeyOrder();
  void testParseHTTPSResponseRejectsBadParamLength();
  void testParseHTTPSResponseRejectsNoDefaultAlpnWithoutAlpn();
  void testParseHTTPSResponseMandatoryParam();
  void testParseHTTPSResponseRejectsBadMandatoryParam();
  void testParseHTTPSResponseIgnoresUnknownMandatoryParam();
  void testParseHTTPSResponseRootTargetName();
  void testParseHTTPSResponseAliasModeIgnoresParams();
  void testParseHTTPSResponseAcceptsCNAMEChain();
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

void appendSvcParam(std::string& out, uint16_t key, const std::string& value)
{
  appendUint16(out, key);
  appendUint16(out, static_cast<uint16_t>(value.size()));
  out.append(value);
}

void appendServiceBindingAnswer(std::string& out, uint16_t type,
                                uint16_t nameOffset, uint32_t ttl,
                                uint16_t priority,
                                const std::string& targetName,
                                const std::string& params)
{
  std::string rdata;
  appendUint16(rdata, priority);
  appendName(rdata, targetName);
  rdata.append(params);

  appendCompressedName(out, nameOffset);
  appendUint16(out, type);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, static_cast<uint16_t>(rdata.size()));
  out.append(rdata);
}

void appendServiceBindingAnswer(std::string& out, uint16_t type,
                                const std::string& name, uint32_t ttl,
                                uint16_t priority,
                                const std::string& targetName,
                                const std::string& params)
{
  std::string rdata;
  appendUint16(rdata, priority);
  appendName(rdata, targetName);
  rdata.append(params);

  appendName(out, name);
  appendUint16(out, type);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, static_cast<uint16_t>(rdata.size()));
  out.append(rdata);
}

void appendCompressedServiceBindingAnswer(std::string& out, uint16_t type,
                                          uint16_t nameOffset, uint32_t ttl,
                                          uint16_t priority,
                                          uint16_t targetNameOffset,
                                          const std::string& params)
{
  std::string rdata;
  appendUint16(rdata, priority);
  appendCompressedName(rdata, targetNameOffset);
  rdata.append(params);

  appendCompressedName(out, nameOffset);
  appendUint16(out, type);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, static_cast<uint16_t>(rdata.size()));
  out.append(rdata);
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

std::vector<dns::ServiceBindingRecord>
parseServiceBinding(const std::string& msg, uint16_t id, dns::QueryType qtype,
                    const std::string& hostname = "www.example.com")
{
  return dns::parseServiceBindingResponse(
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

void DnsMessageTest::testCreateSVCBQuery()
{
  std::string expected;
  appendHeader(expected, 0x3455, 0x0100, 1, 0);
  appendQuestion(expected, "www.example.com", dns::TYPE_SVCB);

  CPPUNIT_ASSERT_EQUAL(expected,
                       dns::createQuery(0x3455, "www.example.com",
                                        dns::TYPE_SVCB));
}

void DnsMessageTest::testCreateHTTPSQuery()
{
  std::string expected;
  appendHeader(expected, 0x3456, 0x0100, 1, 0);
  appendQuestion(expected, "www.example.com", dns::TYPE_HTTPS);

  CPPUNIT_ASSERT_EQUAL(expected,
                       dns::createQuery(0x3456, "www.example.com",
                                        dns::TYPE_HTTPS));
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

void DnsMessageTest::testParseBasicSVCBResponse()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_SVCB, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_SVCB, 12, 60, 1,
                             "svc.example.com", std::string());

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_SVCB);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"), result[0].ownerName);
  CPPUNIT_ASSERT_EQUAL((uint32_t)60, result[0].ttl);
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, result[0].priority);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.com"), result[0].targetName);
}

void DnsMessageTest::testParseBasicHTTPSResponse()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", std::string());

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"), result[0].ownerName);
  CPPUNIT_ASSERT_EQUAL((uint32_t)60, result[0].ttl);
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, result[0].priority);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.com"), result[0].targetName);
  CPPUNIT_ASSERT(result[0].alpn.empty());
  CPPUNIT_ASSERT(!result[0].noDefaultAlpn);
  CPPUNIT_ASSERT(!result[0].hasPort);
}

void DnsMessageTest::testParseHTTPSResponseParams()
{
  std::string alpn;
  alpn.push_back(2);
  alpn.append("h2");
  alpn.push_back(8);
  alpn.append("http/1.1");

  std::string port;
  appendUint16(port, 8443);

  std::string ipv4hint;
  ipv4hint.push_back(static_cast<char>(0xc0));
  ipv4hint.push_back(static_cast<char>(0x00));
  ipv4hint.push_back(static_cast<char>(0x02));
  ipv4hint.push_back(static_cast<char>(0x01));
  ipv4hint.push_back(static_cast<char>(0xc6));
  ipv4hint.push_back(static_cast<char>(0x33));
  ipv4hint.push_back(static_cast<char>(0x64));
  ipv4hint.push_back(static_cast<char>(0x02));

  std::string ech;
  ech.push_back(static_cast<char>(0x01));
  ech.push_back(static_cast<char>(0x02));
  ech.push_back(static_cast<char>(0x03));
  ech.push_back(static_cast<char>(0x04));

  std::string ipv6hint;
  static const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 1};
  ipv6hint.append(reinterpret_cast<const char*>(addr), 16);

  std::string params;
  appendSvcParam(params, 1, alpn);
  appendSvcParam(params, 2, std::string());
  appendSvcParam(params, 3, port);
  appendSvcParam(params, 4, ipv4hint);
  appendSvcParam(params, 5, ech);
  appendSvcParam(params, 6, ipv6hint);

  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 2,
                             "svc.example.com", params);

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)2, result[0].priority);
  CPPUNIT_ASSERT_EQUAL((size_t)6, result[0].paramKeys.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, result[0].paramKeys[0]);
  CPPUNIT_ASSERT_EQUAL((uint16_t)6, result[0].paramKeys[5]);
  CPPUNIT_ASSERT_EQUAL((size_t)2, result[0].alpn.size());
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), result[0].alpn[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), result[0].alpn[1]);
  CPPUNIT_ASSERT(result[0].noDefaultAlpn);
  CPPUNIT_ASSERT(result[0].hasPort);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, result[0].port);
  CPPUNIT_ASSERT_EQUAL((size_t)2, result[0].ipv4hint.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), result[0].ipv4hint[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.2"), result[0].ipv4hint[1]);
  CPPUNIT_ASSERT_EQUAL(ech, result[0].echConfigList);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result[0].ipv6hint.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), result[0].ipv6hint[0]);
}

void DnsMessageTest::testParseHTTPSResponseUnknownParam()
{
  std::string params;
  appendSvcParam(params, 65400, std::string("opaque"));

  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params);

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL((size_t)1, result[0].unknownParams.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)65400, result[0].unknownParams[0].key);
  CPPUNIT_ASSERT_EQUAL(std::string("opaque"), result[0].unknownParams[0].value);
}

void DnsMessageTest::testParseHTTPSResponseCompressedTargetName()
{
  std::string msg;
  appendHeader(msg, 0x1234, 0x8180, 1, 1);
  appendQuestion(msg, "www.example.com", dns::TYPE_HTTPS);
  appendCompressedServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1, 12,
                                       std::string());

  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);
}

void DnsMessageTest::testParseHTTPSResponseRejectsBadParamKeyOrder()
{
  std::string port;
  appendUint16(port, 8443);

  std::string alpn;
  alpn.push_back(2);
  alpn.append("h2");

  std::string duplicate;
  appendSvcParam(duplicate, 3, port);
  appendSvcParam(duplicate, 3, port);
  auto msg1 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg1, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", duplicate);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg1, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string outOfOrder;
  appendSvcParam(outOfOrder, 3, port);
  appendSvcParam(outOfOrder, 1, alpn);
  auto msg2 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg2, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", outOfOrder);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg2, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);
}

void DnsMessageTest::testParseHTTPSResponseRejectsBadParamLength()
{
  std::string emptyAlpnParam;
  appendSvcParam(emptyAlpnParam, 1, std::string());
  auto msgEmptyAlpn = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msgEmptyAlpn, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", emptyAlpnParam);
  CPPUNIT_ASSERT_THROW(
      parseServiceBinding(msgEmptyAlpn, 0x1234, dns::TYPE_HTTPS), DlAbortEx);

  std::string zeroAlpn;
  zeroAlpn.push_back(0);
  std::string zeroAlpnParam;
  appendSvcParam(zeroAlpnParam, 1, zeroAlpn);
  auto msgZeroAlpn = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msgZeroAlpn, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", zeroAlpnParam);
  CPPUNIT_ASSERT_THROW(
      parseServiceBinding(msgZeroAlpn, 0x1234, dns::TYPE_HTTPS), DlAbortEx);

  std::string badAlpn;
  badAlpn.push_back(4);
  badAlpn.append("h2");
  std::string badAlpnParam;
  appendSvcParam(badAlpnParam, 1, badAlpn);
  auto msg0 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg0, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", badAlpnParam);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg0, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string badNoDefaultAlpn;
  appendSvcParam(badNoDefaultAlpn, 2, std::string("x"));
  auto msg1 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg1, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", badNoDefaultAlpn);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg1, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string badPort;
  appendSvcParam(badPort, 3, std::string("x"));
  auto msg2 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg2, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", badPort);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg2, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string badIpv4;
  appendSvcParam(badIpv4, 4, std::string("xxx"));
  auto msg3 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg3, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", badIpv4);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg3, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string emptyIpv4;
  appendSvcParam(emptyIpv4, 4, std::string());
  auto msgEmptyIpv4 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msgEmptyIpv4, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", emptyIpv4);
  CPPUNIT_ASSERT_THROW(
      parseServiceBinding(msgEmptyIpv4, 0x1234, dns::TYPE_HTTPS), DlAbortEx);

  std::string badIpv6;
  appendSvcParam(badIpv6, 6, std::string("xxx"));
  auto msg4 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg4, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", badIpv6);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg4, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string emptyIpv6;
  appendSvcParam(emptyIpv6, 6, std::string());
  auto msgEmptyIpv6 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msgEmptyIpv6, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", emptyIpv6);
  CPPUNIT_ASSERT_THROW(
      parseServiceBinding(msgEmptyIpv6, 0x1234, dns::TYPE_HTTPS), DlAbortEx);
}

void DnsMessageTest::testParseHTTPSResponseRejectsNoDefaultAlpnWithoutAlpn()
{
  std::string params;
  appendSvcParam(params, 2, std::string());
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params);

  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);
}

void DnsMessageTest::testParseHTTPSResponseMandatoryParam()
{
  std::string mandatory;
  appendUint16(mandatory, 1);
  appendUint16(mandatory, 3);

  std::string alpn;
  alpn.push_back(2);
  alpn.append("h2");

  std::string port;
  appendUint16(port, 8443);

  std::string params;
  appendSvcParam(params, 0, mandatory);
  appendSvcParam(params, 1, alpn);
  appendSvcParam(params, 3, port);

  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params);

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL((size_t)2, result[0].mandatoryKeys.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, result[0].mandatoryKeys[0]);
  CPPUNIT_ASSERT_EQUAL((uint16_t)3, result[0].mandatoryKeys[1]);
}

void DnsMessageTest::testParseHTTPSResponseRejectsBadMandatoryParam()
{
  std::string emptyMandatory;
  std::string params0;
  appendSvcParam(params0, 0, emptyMandatory);
  auto msg0 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg0, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params0);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg0, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string oddMandatory;
  oddMandatory.push_back(1);
  std::string paramsOdd;
  appendSvcParam(paramsOdd, 0, oddMandatory);
  auto msgOdd = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msgOdd, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", paramsOdd);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msgOdd, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string includesMandatory;
  appendUint16(includesMandatory, 0);
  std::string params1;
  appendSvcParam(params1, 0, includesMandatory);
  auto msg1 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg1, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params1);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg1, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string duplicate;
  appendUint16(duplicate, 1);
  appendUint16(duplicate, 1);
  std::string params2;
  appendSvcParam(params2, 0, duplicate);
  auto msg2 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg2, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params2);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg2, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);

  std::string missing;
  appendUint16(missing, 3);
  std::string alpn;
  alpn.push_back(2);
  alpn.append("h2");
  std::string params3;
  appendSvcParam(params3, 0, missing);
  appendSvcParam(params3, 1, alpn);
  auto msg3 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg3, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params3);
  CPPUNIT_ASSERT_THROW(parseServiceBinding(msg3, 0x1234, dns::TYPE_HTTPS),
                       DlAbortEx);
}

void DnsMessageTest::testParseHTTPSResponseIgnoresUnknownMandatoryParam()
{
  std::string mandatory;
  appendUint16(mandatory, 65400);
  std::string params;
  appendSvcParam(params, 0, mandatory);
  appendSvcParam(params, 65400, std::string("opaque"));

  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 1,
                             "svc.example.com", params);

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT(result.empty());
}

void DnsMessageTest::testParseHTTPSResponseRootTargetName()
{
  auto msg1 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg1, dns::TYPE_HTTPS, 12, 60, 1, std::string(),
                             std::string());

  auto result1 = parseServiceBinding(msg1, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result1.size());
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"), result1[0].targetName);
  CPPUNIT_ASSERT(!result1[0].aliasModeUnavailable);

  auto msg2 = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg2, dns::TYPE_HTTPS, 12, 60, 0, std::string(),
                             std::string());

  auto result2 = parseServiceBinding(msg2, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result2.size());
  CPPUNIT_ASSERT_EQUAL(std::string(""), result2[0].targetName);
  CPPUNIT_ASSERT(result2[0].aliasModeUnavailable);
}

void DnsMessageTest::testParseHTTPSResponseAliasModeIgnoresParams()
{
  std::string params;
  appendSvcParam(params, 3, std::string("x"));
  appendSvcParam(params, 1, std::string());

  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 1);
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, 12, 60, 0,
                             "alias.example.com", params);

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)0, result[0].priority);
  CPPUNIT_ASSERT_EQUAL(std::string("alias.example.com"), result[0].targetName);
  CPPUNIT_ASSERT(result[0].paramKeys.empty());
}

void DnsMessageTest::testParseHTTPSResponseAcceptsCNAMEChain()
{
  auto msg = createResponseHeader(0x1234, 0x8180, dns::TYPE_HTTPS, 2);
  appendCNAMEAnswer(msg, 12, 60, "alias.example.com");
  appendServiceBindingAnswer(msg, dns::TYPE_HTTPS, "alias.example.com", 60, 1,
                             "svc.example.com", std::string());

  auto result = parseServiceBinding(msg, 0x1234, dns::TYPE_HTTPS);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.com"), result[0].targetName);
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
