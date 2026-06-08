#include "HttpProtocol.h"

#include <string>

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"

namespace aria2 {

class HttpProtocolTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpProtocolTest);
  CPPUNIT_TEST(testAlpnProtocolNames);
  CPPUNIT_TEST(testHttpProtocolFromSelectedAlpn);
  CPPUNIT_TEST(testValidateHttpSelectedAlpnProtocol);
  CPPUNIT_TEST_SUITE_END();

public:
  void testAlpnProtocolNames();
  void testHttpProtocolFromSelectedAlpn();
  void testValidateHttpSelectedAlpnProtocol();
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

void HttpProtocolTest::testValidateHttpSelectedAlpnProtocol()
{
  validateHttpSelectedAlpnProtocol("");
  validateHttpSelectedAlpnProtocol(HTTP_ALPN_HTTP11);
  CPPUNIT_ASSERT_THROW(validateHttpSelectedAlpnProtocol(HTTP_ALPN_H2),
                       Exception);
  CPPUNIT_ASSERT_THROW(validateHttpSelectedAlpnProtocol("h3"), Exception);
}

} // namespace aria2
