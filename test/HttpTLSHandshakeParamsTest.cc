#include "HttpTLSHandshakeParams.h"

#ifdef ENABLE_SSL

#include <string>

#include <cppunit/extensions/HelperMacros.h>

#include "HttpProtocol.h"
#include "Option.h"
#include "Request.h"
#include "prefs.h"

namespace aria2 {

class HttpTLSHandshakeParamsTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpTLSHandshakeParamsTest);
  CPPUNIT_TEST(testDefaultHost);
  CPPUNIT_TEST(testDefaultAlpnProtocolsEmpty);
  CPPUNIT_TEST(testHttp2AlpnProtocolsWhenEnabled);
  CPPUNIT_TEST(testHttp2AlpnProtocolsDisabledByPipelining);
  CPPUNIT_TEST(testHostsMappingControlsVerifyHost);
  CPPUNIT_TEST(testExplicitSNIOverridesMappedVerifyHost);
  CPPUNIT_TEST(testMappedSNIRequestHostBeatsDefaultHost);
  CPPUNIT_TEST_SUITE_END();

public:
  void testDefaultHost();
  void testDefaultAlpnProtocolsEmpty();
  void testHttp2AlpnProtocolsWhenEnabled();
  void testHttp2AlpnProtocolsDisabledByPipelining();
  void testHostsMappingControlsVerifyHost();
  void testExplicitSNIOverridesMappedVerifyHost();
  void testMappedSNIRequestHostBeatsDefaultHost();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpTLSHandshakeParamsTest);

void HttpTLSHandshakeParamsTest::testDefaultHost()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
  CPPUNIT_ASSERT(!params.sniHostOverridden);
}

void HttpTLSHandshakeParamsTest::testDefaultAlpnProtocolsEmpty()
{
  Option option;

  auto protocols = createHttpAlpnProtocols(&option);

  CPPUNIT_ASSERT(protocols.empty());
}

void HttpTLSHandshakeParamsTest::testHttp2AlpnProtocolsWhenEnabled()
{
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);

  auto protocols = createHttpAlpnProtocols(&option);

#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT_EQUAL((size_t)2, protocols.size());
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_H2), protocols[0]);
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_HTTP11), protocols[1]);
#else  // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(protocols.empty());
#endif // !HAVE_LIBNGHTTP2
}

void HttpTLSHandshakeParamsTest::testHttp2AlpnProtocolsDisabledByPipelining()
{
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTP_PIPELINING, A2_V_TRUE);

  auto protocols = createHttpAlpnProtocols(&option);

  CPPUNIT_ASSERT(protocols.empty());
}

void HttpTLSHandshakeParamsTest::testHostsMappingControlsVerifyHost()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://198.18.0.18/file"));
  Option option;
  option.put(PREF_HOSTS_MAPPING, "198.18.0.18:origin.example");

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
  CPPUNIT_ASSERT(!params.sniHostOverridden);
}

void HttpTLSHandshakeParamsTest::testExplicitSNIOverridesMappedVerifyHost()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://198.18.0.18/file"));
  Option option;
  option.put(PREF_HOSTS_MAPPING, "198.18.0.18:origin.example");
  option.put(PREF_TLS_SNI_HOST, "front.example");

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT_EQUAL(std::string("front.example"), params.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
  CPPUNIT_ASSERT(params.sniHostOverridden);
}

void HttpTLSHandshakeParamsTest::testMappedSNIRequestHostBeatsDefaultHost()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://198.18.0.18/file"));
  Option option;
  option.put(PREF_HOSTS_MAPPING, "198.18.0.18:origin.example");
  option.put(PREF_TLS_SNI_HOST,
             "origin.example:front.example,198.18.0.18:ip-front.example");

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT_EQUAL(std::string("ip-front.example"), params.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
  CPPUNIT_ASSERT(params.sniHostOverridden);
}

} // namespace aria2

#endif // ENABLE_SSL
