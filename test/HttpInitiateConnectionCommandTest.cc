#include "HttpInitiateConnectionCommand.h"

#include <cppunit/extensions/HelperMacros.h>

#include "Request.h"
#include "ServiceBindingSelector.h"

namespace aria2 {

class HttpInitiateConnectionCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpInitiateConnectionCommandTest);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesOriginByDefault);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesProxy);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesSvcbEndpoint);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityIgnoresHttpEndpoint);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityIgnoresMismatchedEndpoint);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSelectHttpConnectionAuthorityUsesOriginByDefault();
  void testSelectHttpConnectionAuthorityUsesProxy();
  void testSelectHttpConnectionAuthorityUsesSvcbEndpoint();
  void testSelectHttpConnectionAuthorityIgnoresHttpEndpoint();
  void testSelectHttpConnectionAuthorityIgnoresMismatchedEndpoint();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpInitiateConnectionCommandTest);

namespace {
std::shared_ptr<Request> makeRequest(const std::string& uri)
{
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri(uri));
  return request;
}

dns::ServiceBindingEndpoint makeEndpoint(
    const std::string& originHost, uint16_t originPort,
    const std::string& connectHost, uint16_t connectPort)
{
  dns::ServiceBindingEndpoint endpoint;
  endpoint.originHost = originHost;
  endpoint.originPort = originPort;
  endpoint.connectHost = connectHost;
  endpoint.connectPort = connectPort;
  return endpoint;
}
} // namespace

void HttpInitiateConnectionCommandTest::
    testSelectHttpConnectionAuthorityUsesOriginByDefault()
{
  auto request = makeRequest("https://origin.example/file");

  auto authority =
      selectHttpConnectionAuthority(request.get(), nullptr,
                                    std::vector<dns::ServiceBindingEndpoint>());

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, authority.port);
}

void HttpInitiateConnectionCommandTest::
    testSelectHttpConnectionAuthorityUsesProxy()
{
  auto request = makeRequest("https://origin.example/file");
  auto proxyRequest = makeRequest("http://proxy.example:8080/");
  std::vector<dns::ServiceBindingEndpoint> endpoints;
  endpoints.push_back(
      makeEndpoint("origin.example", 443, "svc.example", 8443));

  auto authority =
      selectHttpConnectionAuthority(request.get(), proxyRequest.get(),
                                    endpoints);

  CPPUNIT_ASSERT_EQUAL(std::string("proxy.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8080, authority.port);
}

void HttpInitiateConnectionCommandTest::
    testSelectHttpConnectionAuthorityUsesSvcbEndpoint()
{
  auto request = makeRequest("https://origin.example/file");
  std::vector<dns::ServiceBindingEndpoint> endpoints;
  endpoints.push_back(
      makeEndpoint("origin.example", 443, "svc.example", 8443));

  auto authority =
      selectHttpConnectionAuthority(request.get(), nullptr, endpoints);

  CPPUNIT_ASSERT_EQUAL(std::string("svc.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, authority.port);
}

void HttpInitiateConnectionCommandTest::
    testSelectHttpConnectionAuthorityIgnoresHttpEndpoint()
{
  auto request = makeRequest("http://origin.example/file");
  std::vector<dns::ServiceBindingEndpoint> endpoints;
  endpoints.push_back(makeEndpoint("origin.example", 80, "svc.example", 8080));

  auto authority =
      selectHttpConnectionAuthority(request.get(), nullptr, endpoints);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)80, authority.port);
}

void HttpInitiateConnectionCommandTest::
    testSelectHttpConnectionAuthorityIgnoresMismatchedEndpoint()
{
  auto request = makeRequest("https://origin.example/file");
  std::vector<dns::ServiceBindingEndpoint> endpoints;
  endpoints.push_back(
      makeEndpoint("other.example", 443, "svc.example", 8443));

  auto authority =
      selectHttpConnectionAuthority(request.get(), nullptr, endpoints);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, authority.port);
}

} // namespace aria2
