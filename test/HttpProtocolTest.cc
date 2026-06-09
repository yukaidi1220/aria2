#include "HttpProtocol.h"

#include <string>

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"

namespace aria2 {

class HttpProtocolTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpProtocolTest);
  CPPUNIT_TEST(testAlpnProtocolNames);
  CPPUNIT_TEST(testHttpProtocolFromSelectedAlpn);
  CPPUNIT_TEST(testRequireSupportedHttpProtocolFromSelectedAlpn);
  CPPUNIT_TEST(testDecideHttpProtocolFromSelectedAlpn);
  CPPUNIT_TEST(testValidateHttpSelectedAlpnProtocol);
  CPPUNIT_TEST(testCanSubmitSingleHttp2Stream);
  CPPUNIT_TEST_SUITE_END();

public:
  void testAlpnProtocolNames();
  void testHttpProtocolFromSelectedAlpn();
  void testRequireSupportedHttpProtocolFromSelectedAlpn();
  void testDecideHttpProtocolFromSelectedAlpn();
  void testValidateHttpSelectedAlpnProtocol();
  void testCanSubmitSingleHttp2Stream();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpProtocolTest);

void HttpProtocolTest::testAlpnProtocolNames()
{
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), std::string(HTTP_ALPN_H2));
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"),
                       std::string(HTTP_ALPN_HTTP11));
}

void HttpProtocolTest::testHttpProtocolFromSelectedAlpn()
{
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 == httpProtocolFromSelectedAlpn(""));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 httpProtocolFromSelectedAlpn(HTTP_ALPN_HTTP11));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_H2 == httpProtocolFromSelectedAlpn(HTTP_ALPN_H2));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_UNKNOWN == httpProtocolFromSelectedAlpn("h3"));
}

void HttpProtocolTest::testRequireSupportedHttpProtocolFromSelectedAlpn()
{
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 requireSupportedHttpProtocolFromSelectedAlpn("", false));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 requireSupportedHttpProtocolFromSelectedAlpn(
                     HTTP_ALPN_HTTP11, false));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_H2 ==
                 requireSupportedHttpProtocolFromSelectedAlpn(HTTP_ALPN_H2,
                                                              true));
  CPPUNIT_ASSERT_THROW(requireSupportedHttpProtocolFromSelectedAlpn(
                           HTTP_ALPN_H2, false),
                       Exception);
  CPPUNIT_ASSERT_THROW(requireSupportedHttpProtocolFromSelectedAlpn("h3", true),
                       Exception);
}

void HttpProtocolTest::testDecideHttpProtocolFromSelectedAlpn()
{
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 decideHttpProtocolFromSelectedAlpn("", false));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 decideHttpProtocolFromSelectedAlpn("", true));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 decideHttpProtocolFromSelectedAlpn(HTTP_ALPN_HTTP11, false));
  CPPUNIT_ASSERT(HTTP_PROTOCOL_HTTP1 ==
                 decideHttpProtocolFromSelectedAlpn(HTTP_ALPN_HTTP11, true));

  CPPUNIT_ASSERT_THROW(
      decideHttpProtocolFromSelectedAlpn(HTTP_ALPN_H2, false), Exception);
#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(HTTP_PROTOCOL_H2 ==
                 decideHttpProtocolFromSelectedAlpn(HTTP_ALPN_H2, true));
#else  // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT_THROW(decideHttpProtocolFromSelectedAlpn(HTTP_ALPN_H2, true),
                       Exception);
#endif // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT_THROW(decideHttpProtocolFromSelectedAlpn("h3", false),
                       Exception);
  CPPUNIT_ASSERT_THROW(decideHttpProtocolFromSelectedAlpn("h3", true),
                       Exception);
}

void HttpProtocolTest::testValidateHttpSelectedAlpnProtocol()
{
  validateHttpSelectedAlpnProtocol("");
  validateHttpSelectedAlpnProtocol(HTTP_ALPN_HTTP11);
  CPPUNIT_ASSERT_THROW(validateHttpSelectedAlpnProtocol(HTTP_ALPN_H2),
                       Exception);
  CPPUNIT_ASSERT_THROW(validateHttpSelectedAlpnProtocol("h3"), Exception);
}

void HttpProtocolTest::testCanSubmitSingleHttp2Stream()
{
  CPPUNIT_ASSERT(canSubmitSingleHttp2Stream(0));
  CPPUNIT_ASSERT(canSubmitSingleHttp2Stream(1));
  CPPUNIT_ASSERT(!canSubmitSingleHttp2Stream(2));
}

} // namespace aria2
