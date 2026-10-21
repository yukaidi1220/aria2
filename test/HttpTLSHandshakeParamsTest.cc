#include "HttpTLSHandshakeParams.h"

#ifdef ENABLE_SSL

#include <string>

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"
#include "HttpProtocol.h"
#include "Option.h"
#include "Request.h"
#include "a2netcompat.h"
#include "prefs.h"

namespace aria2 {

class HttpTLSHandshakeParamsTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpTLSHandshakeParamsTest);
  CPPUNIT_TEST(testDefaultHost);
  CPPUNIT_TEST(testDefaultECHParamsEmpty);
  CPPUNIT_TEST(testDefaultAlpnProtocolsEmpty);
  CPPUNIT_TEST(testHttp2AlpnProtocolsWhenEnabled);
  CPPUNIT_TEST(testHttp2AlpnProtocolsDisabledByPipelining);
  CPPUNIT_TEST(testDohHttp2DoesNotEnableDownloadAlpn);
  CPPUNIT_TEST(testHttpsServiceBindingHttp11RestrictsAlpn);
  CPPUNIT_TEST(testHttpsServiceBindingH2RestrictsAlpn);
  CPPUNIT_TEST(testHttpsServiceBindingDefaultAlpnKeepsConfiguredAlpn);
  CPPUNIT_TEST(testECHConfigBase64EnablesRequiredECH);
  CPPUNIT_TEST(testEnableECHRequiresConfig);
  CPPUNIT_TEST(testECHRejectsBadBase64);
  CPPUNIT_TEST(testECHRejectsSNIOverride);
  CPPUNIT_TEST(testHostsMappingControlsVerifyHost);
  CPPUNIT_TEST(testExplicitSNIOverridesMappedVerifyHost);
  CPPUNIT_TEST(testMappedSNIRequestHostBeatsDefaultHost);
  CPPUNIT_TEST(testTlsAddressFailoverSwitchesFamilyWhenVerifying);
  CPPUNIT_TEST(testTlsAddressFailoverAbortsWhenOtherFamilyUnavailable);
  CPPUNIT_TEST(testTlsAddressFailoverAnyAddressWhenNotVerifying);
  CPPUNIT_TEST(testTlsAddressFailoverAbortsUnknownFamily);
  CPPUNIT_TEST(testTlsAddressFailoverBlockedBySNIOverride);
  CPPUNIT_TEST(testShouldRetryTLSHandshakeWithoutSNIEnabled);
  CPPUNIT_TEST(testShouldRetryTLSHandshakeWithoutSNIGuards);
  CPPUNIT_TEST(testCreateHttpTLSHandshakeParamsRetryMode);
  CPPUNIT_TEST_SUITE_END();

public:
  void testDefaultHost();
  void testDefaultECHParamsEmpty();
  void testDefaultAlpnProtocolsEmpty();
  void testHttp2AlpnProtocolsWhenEnabled();
  void testHttp2AlpnProtocolsDisabledByPipelining();
  void testDohHttp2DoesNotEnableDownloadAlpn();
  void testHttpsServiceBindingHttp11RestrictsAlpn();
  void testHttpsServiceBindingH2RestrictsAlpn();
  void testHttpsServiceBindingDefaultAlpnKeepsConfiguredAlpn();
  void testECHConfigBase64EnablesRequiredECH();
  void testEnableECHRequiresConfig();
  void testECHRejectsBadBase64();
  void testECHRejectsSNIOverride();
  void testHostsMappingControlsVerifyHost();
  void testExplicitSNIOverridesMappedVerifyHost();
  void testMappedSNIRequestHostBeatsDefaultHost();
  void testTlsAddressFailoverSwitchesFamilyWhenVerifying();
  void testTlsAddressFailoverAbortsWhenOtherFamilyUnavailable();
  void testTlsAddressFailoverAnyAddressWhenNotVerifying();
  void testTlsAddressFailoverAbortsUnknownFamily();
  void testTlsAddressFailoverBlockedBySNIOverride();
  void testShouldRetryTLSHandshakeWithoutSNIEnabled();
  void testShouldRetryTLSHandshakeWithoutSNIGuards();
  void testCreateHttpTLSHandshakeParamsRetryMode();
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

void HttpTLSHandshakeParamsTest::testDefaultECHParamsEmpty()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT(!params.echParams.requested);
  CPPUNIT_ASSERT(!params.echParams.required);
  CPPUNIT_ASSERT(params.echParams.configList.empty());
  CPPUNIT_ASSERT(params.echParams.source.empty());
  CPPUNIT_ASSERT(params.echParams.outerName.empty());
  CPPUNIT_ASSERT(params.echParams.outerAlpnProtocols.empty());
  CPPUNIT_ASSERT(params.echParams.retryConfigList.empty());
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

void HttpTLSHandshakeParamsTest::testDohHttp2DoesNotEnableDownloadAlpn()
{
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_FALSE);
  option.put(PREF_ENABLE_DOH_HTTP2, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTP_PIPELINING, A2_V_FALSE);

  auto protocols = createHttpAlpnProtocols(&option);

  CPPUNIT_ASSERT(protocols.empty());
}

void HttpTLSHandshakeParamsTest::testHttpsServiceBindingHttp11RestrictsAlpn()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  request.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                             "svc.example", 8443,
                                             HTTP_ALPN_HTTP11, false);
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT_EQUAL((size_t)1, params.alpnProtocols.size());
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_HTTP11),
                       params.alpnProtocols[0]);
}

void HttpTLSHandshakeParamsTest::testHttpsServiceBindingH2RestrictsAlpn()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  request.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                             "svc.example", 8443,
                                             HTTP_ALPN_H2, false);
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);

  auto params = createHttpTLSHandshakeParams(&request, &option);

#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT_EQUAL((size_t)1, params.alpnProtocols.size());
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_H2), params.alpnProtocols[0]);
#else  // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
#endif // !HAVE_LIBNGHTTP2
}

void HttpTLSHandshakeParamsTest::
    testHttpsServiceBindingDefaultAlpnKeepsConfiguredAlpn()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  request.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                             "svc.example", 8443,
                                             HTTP_ALPN_HTTP11, true);
  Option option;
  option.put(PREF_ENABLE_HTTP2, A2_V_TRUE);

  auto params = createHttpTLSHandshakeParams(&request, &option);

#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT_EQUAL((size_t)2, params.alpnProtocols.size());
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_H2), params.alpnProtocols[0]);
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_HTTP11),
                       params.alpnProtocols[1]);
#else  // !HAVE_LIBNGHTTP2
  CPPUNIT_ASSERT(params.alpnProtocols.empty());
#endif // !HAVE_LIBNGHTTP2
}

void HttpTLSHandshakeParamsTest::testECHConfigBase64EnablesRequiredECH()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_ECH_CONFIG_BASE64, "AQIDBA==");

  auto params = createHttpTLSHandshakeParams(&request, &option);

  CPPUNIT_ASSERT(params.echParams.requested);
  CPPUNIT_ASSERT(params.echParams.required);
  CPPUNIT_ASSERT_EQUAL(std::string("\x01\x02\x03\x04", 4),
                       params.echParams.configList);
  CPPUNIT_ASSERT_EQUAL(std::string("manual"), params.echParams.source);
}

void HttpTLSHandshakeParamsTest::testEnableECHRequiresConfig()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_ENABLE_ECH, A2_V_TRUE);

  CPPUNIT_ASSERT_THROW(createHttpTLSHandshakeParams(&request, &option),
                       Exception);
}

void HttpTLSHandshakeParamsTest::testECHRejectsBadBase64()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_ECH_CONFIG_BASE64, "not/base64");

  CPPUNIT_ASSERT_THROW(createHttpTLSHandshakeParams(&request, &option),
                       Exception);
}

void HttpTLSHandshakeParamsTest::testECHRejectsSNIOverride()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_ECH_CONFIG_BASE64, "AQIDBA==");
  option.put(PREF_TLS_SNI_HOST, "front.example");

  CPPUNIT_ASSERT_THROW(createHttpTLSHandshakeParams(&request, &option),
                       Exception);
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

void HttpTLSHandshakeParamsTest::
    testTlsAddressFailoverSwitchesFamilyWhenVerifying()
{
  // IPv4 handshake failed, IPv6 address cached and not penalized: switch
  // to the other address family.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_OTHER_FAMILY,
                       decideTlsAddressFailover(AF_INET, true, true, false,
                                                true));
  // IPv6 handshake failed, IPv4 address cached: switch the other way.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_OTHER_FAMILY,
                       decideTlsAddressFailover(AF_INET6, true, true, false,
                                                true));
}

void HttpTLSHandshakeParamsTest::
    testTlsAddressFailoverAbortsWhenOtherFamilyUnavailable()
{
  // No cached address of the other address family.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ABORT,
                       decideTlsAddressFailover(AF_INET, true, false, false,
                                                true));
  // Other family already failed recently (both families failed): abort
  // instead of looping through addresses.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ABORT,
                       decideTlsAddressFailover(AF_INET, true, true, true,
                                                true));
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ABORT,
                       decideTlsAddressFailover(AF_INET6, true, true, true,
                                                true));
}

void HttpTLSHandshakeParamsTest::
    testTlsAddressFailoverAnyAddressWhenNotVerifying()
{
  // Certificate verification disabled: any other resolved address of the
  // same host may be retried, regardless of family penalty state.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ANY_ADDRESS,
                       decideTlsAddressFailover(AF_INET, false, false, false,
                                                true));
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ANY_ADDRESS,
                       decideTlsAddressFailover(AF_INET6, false, true, true,
                                                true));
  // ... but not when no other address remains.
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ABORT,
                       decideTlsAddressFailover(AF_INET, false, false, false,
                                                false));
}

void HttpTLSHandshakeParamsTest::testTlsAddressFailoverAbortsUnknownFamily()
{
  CPPUNIT_ASSERT_EQUAL(TLS_ADDRESS_FAILOVER_ABORT,
                       decideTlsAddressFailover(0, true, true, false, true));
}

void HttpTLSHandshakeParamsTest::testTlsAddressFailoverBlockedBySNIOverride()
{
  // No override: address rotation stays allowed.
  TLSHandshakeParams plain("origin.example", "origin.example");
  CPPUNIT_ASSERT(tlsAddressFailoverAllowed(plain));
  // An explicit override blocks rotation...
  TLSHandshakeParams overridden("front.example", "origin.example", true);
  CPPUNIT_ASSERT(!tlsAddressFailoverAllowed(overridden));
  // ... even when the SNI hostname is identical to the request hostname.
  TLSHandshakeParams sameHost("origin.example", "origin.example", true);
  CPPUNIT_ASSERT(!tlsAddressFailoverAllowed(sameHost));
  // Omitting SNI does not block rotation.
  TLSHandshakeParams omitted("", "origin.example", true, true);
  CPPUNIT_ASSERT(tlsAddressFailoverAllowed(omitted));
}

void HttpTLSHandshakeParamsTest::testShouldRetryTLSHandshakeWithoutSNIEnabled()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_TRUE);

  CPPUNIT_ASSERT(shouldRetryTLSHandshakeWithoutSNI(&request, &option, true));
}

void HttpTLSHandshakeParamsTest::testShouldRetryTLSHandshakeWithoutSNIGuards()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_TRUE);

  // The TLS backend of the failed session cannot omit SNI.
  CPPUNIT_ASSERT(!shouldRetryTLSHandshakeWithoutSNI(&request, &option, false));
  // Disabled by --tls-sni-none-fallback=false.
  option.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_FALSE);
  CPPUNIT_ASSERT(!shouldRetryTLSHandshakeWithoutSNI(&request, &option, true));
  option.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_TRUE);
  // Already retried once without SNI: no second fallback, otherwise the
  // retry would loop.
  request.setTlsSNINoneRetry(true);
  CPPUNIT_ASSERT(!shouldRetryTLSHandshakeWithoutSNI(&request, &option, true));
  request.setTlsSNINoneRetry(false);
  // SNI is already omitted: nothing to fall back from.
  option.put(PREF_TLS_SNI_HOST, "none");
  CPPUNIT_ASSERT(!shouldRetryTLSHandshakeWithoutSNI(&request, &option, true));
  // Only HTTPS is covered.
  Request plainRequest;
  CPPUNIT_ASSERT(plainRequest.setUri("http://origin.example/file"));
  CPPUNIT_ASSERT(
      !shouldRetryTLSHandshakeWithoutSNI(&plainRequest, &option, true));
  // ECH owns the outer name and cannot be combined with an override.
  Option echOption;
  echOption.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_TRUE);
  echOption.put(PREF_ECH_CONFIG_BASE64, "AQIDBA==");
  CPPUNIT_ASSERT(!shouldRetryTLSHandshakeWithoutSNI(&request, &echOption,
                                                    true));
  // ECH combined with an SNI override is a misconfiguration: the same
  // DlAbortEx a normal handshake setup would raise, before any retry.
  Option echOverrideOption;
  echOverrideOption.put(PREF_TLS_SNI_NONE_FALLBACK, A2_V_TRUE);
  echOverrideOption.put(PREF_ECH_CONFIG_BASE64, "AQIDBA==");
  echOverrideOption.put(PREF_TLS_SNI_HOST, "front.example");
  CPPUNIT_ASSERT_THROW(
      shouldRetryTLSHandshakeWithoutSNI(&request, &echOverrideOption, true),
      Exception);
}

void HttpTLSHandshakeParamsTest::testCreateHttpTLSHandshakeParamsRetryMode()
{
  Request request;
  CPPUNIT_ASSERT(request.setUri("https://origin.example/file"));
  Option option;
  option.put(PREF_TLS_SNI_HOST, "front.example");

  // Before the fallback the configured override is used...
  auto params = createHttpTLSHandshakeParams(&request, &option);
  CPPUNIT_ASSERT_EQUAL(std::string("front.example"), params.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.sniHostOverridden);
  CPPUNIT_ASSERT(!params.sniHostOmitted);

  // ... and once the no-SNI retry is in flight the override is dropped in
  // favor of omitting the extension; verification is unchanged.
  request.setTlsSNINoneRetry(true);
  params = createHttpTLSHandshakeParams(&request, &option);
  CPPUNIT_ASSERT(params.sniHost.empty());
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
  CPPUNIT_ASSERT(params.sniHostOverridden);
  CPPUNIT_ASSERT(params.sniHostOmitted);
}

} // namespace aria2

#endif // ENABLE_SSL
