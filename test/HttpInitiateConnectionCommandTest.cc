#include "HttpInitiateConnectionCommand.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "A2STR.h"
#include "DownloadEngine.h"
#include "GroupId.h"
#include "Option.h"
#include "RequestGroup.h"
#include "Request.h"
#include "SelectEventPoll.h"
#include "ServiceBindingSelector.h"
#include "SocketCore.h"
#include "a2functional.h"
#include "prefs.h"

namespace aria2 {

class HttpInitiateConnectionCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpInitiateConnectionCommandTest);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesOriginByDefault);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesProxy);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityUsesSvcbEndpoint);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityIgnoresHttpEndpoint);
  CPPUNIT_TEST(testSelectHttpConnectionAuthorityIgnoresMismatchedEndpoint);
  CPPUNIT_TEST(testSelectConnectionAuthorityIgnoresCachedSvcbByDefault);
  CPPUNIT_TEST(testSelectConnectionAuthorityIgnoresCachedSvcbWhenAsyncDnsOff);
  CPPUNIT_TEST(testSelectConnectionAuthorityUsesCachedSvcbEndpoint);
  CPPUNIT_TEST(testSelectConnectionAuthoritySkipsFailedSvcbEndpoint);
  CPPUNIT_TEST(testSelectConnectionAuthorityFallsBackWhenSvcbEndpointsFailed);
  CPPUNIT_TEST(testSelectConnectionAuthorityUsesProxyWithCachedSvcbEndpoint);
  CPPUNIT_TEST(testCreateNextCommandReusesSvcbConnectPortPooledSocket);
  CPPUNIT_TEST(testConnectFailureMarksSvcbEndpointFailed);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSelectHttpConnectionAuthorityUsesOriginByDefault();
  void testSelectHttpConnectionAuthorityUsesProxy();
  void testSelectHttpConnectionAuthorityUsesSvcbEndpoint();
  void testSelectHttpConnectionAuthorityIgnoresHttpEndpoint();
  void testSelectHttpConnectionAuthorityIgnoresMismatchedEndpoint();
  void testSelectConnectionAuthorityIgnoresCachedSvcbByDefault();
  void testSelectConnectionAuthorityIgnoresCachedSvcbWhenAsyncDnsOff();
  void testSelectConnectionAuthorityUsesCachedSvcbEndpoint();
  void testSelectConnectionAuthoritySkipsFailedSvcbEndpoint();
  void testSelectConnectionAuthorityFallsBackWhenSvcbEndpointsFailed();
  void testSelectConnectionAuthorityUsesProxyWithCachedSvcbEndpoint();
  void testCreateNextCommandReusesSvcbConnectPortPooledSocket();
  void testConnectFailureMarksSvcbEndpointFailed();
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

dns::ServiceBindingRecord makeSvcbRecord()
{
  dns::ServiceBindingRecord record;
  record.ownerName = "origin.example";
  record.priority = 1;
  record.targetName = "svc.example";
  record.hasPort = true;
  record.port = 8443;
  record.alpn.push_back("http/1.1");
  record.ipv4hint.push_back("192.0.2.10");
  return record;
}

dns::ServiceBindingRecord makeSvcbRecord(const std::string& targetName,
                                         uint16_t port, uint16_t priority,
                                         const std::string& hint)
{
  dns::ServiceBindingRecord record;
  record.ownerName = "origin.example";
  record.priority = priority;
  record.targetName = targetName;
  record.hasPort = true;
  record.port = port;
  record.alpn.push_back("http/1.1");
  record.ipv4hint.push_back(hint);
  return record;
}

std::pair<std::shared_ptr<SocketCore>, std::shared_ptr<SocketCore>>
createSocketPair()
{
  SocketCore server;
  server.bind(0);
  server.beginListen();
  server.setBlockingMode();

  auto endpoint = server.getAddrInfo();
  auto client = std::make_shared<SocketCore>();
  client->establishConnection("localhost", endpoint.port);
  CPPUNIT_ASSERT(client->isWritable(5));

  auto inbound = server.acceptConnection();
  inbound->setBlockingMode();

  return std::pair<std::shared_ptr<SocketCore>, std::shared_ptr<SocketCore>>(
      client, inbound);
}

class TestHttpInitiateConnectionCommand
    : public HttpInitiateConnectionCommand {
public:
  using AbstractCommand::onAllConnectAddressesFailed;
  using HttpInitiateConnectionCommand::createNextCommand;
  using HttpInitiateConnectionCommand::selectConnectionAuthority;

  TestHttpInitiateConnectionCommand(
      const std::shared_ptr<Request>& request,
      const std::shared_ptr<RequestGroup>& requestGroup, DownloadEngine* e)
      : HttpInitiateConnectionCommand(1, request, nullptr,
                                      requestGroup.get(), e)
  {
  }
};
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

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthorityIgnoresCachedSvcbByDefault()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord());
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, authority.port);
  CPPUNIT_ASSERT(authority.directOrigin);
  CPPUNIT_ASSERT(!request->hasHttpsServiceBindingEndpointInfo());
  CPPUNIT_ASSERT(e.findCachedIPAddress("svc.example", 8443).empty());
}

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthorityUsesCachedSvcbEndpoint()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_TRUE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord());
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);

  CPPUNIT_ASSERT_EQUAL(std::string("svc.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, authority.port);
  CPPUNIT_ASSERT(!authority.directOrigin);
  CPPUNIT_ASSERT(request->hasHttpsServiceBindingEndpointInfo());
  const auto& info = request->getHttpsServiceBindingEndpointInfo();
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), info.originHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, info.originPort);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example"), info.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, info.connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), info.alpn);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.10"),
                       e.findCachedIPAddress("svc.example", 8443));
  CPPUNIT_ASSERT(e.findCachedIPAddress("origin.example", 443).empty());
}

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthorityIgnoresCachedSvcbWhenAsyncDnsOff()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_FALSE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord());
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, authority.port);
  CPPUNIT_ASSERT(authority.directOrigin);
  CPPUNIT_ASSERT(!request->hasHttpsServiceBindingEndpointInfo());
  CPPUNIT_ASSERT(e.findCachedIPAddress("svc.example", 8443).empty());
}

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthoritySkipsFailedSvcbEndpoint()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_TRUE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord("svc1.example", 8443, 1, "192.0.2.10"));
  records.push_back(makeSvcbRecord("svc2.example", 9443, 2, "192.0.2.11"));
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  auto failed = makeEndpoint("origin.example", 443, "svc1.example", 8443);
  failed.alpn = "http/1.1";
  e.markHttpsServiceBindingEndpointFailed(failed, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);

  CPPUNIT_ASSERT_EQUAL(std::string("svc2.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)9443, authority.port);
  CPPUNIT_ASSERT(!authority.directOrigin);
  CPPUNIT_ASSERT(e.findCachedIPAddress("svc1.example", 8443).empty());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.11"),
                       e.findCachedIPAddress("svc2.example", 9443));
}

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthorityFallsBackWhenSvcbEndpointsFailed()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_TRUE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord());
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  auto failed = makeEndpoint("origin.example", 443, "svc.example", 8443);
  failed.alpn = "http/1.1";
  e.markHttpsServiceBindingEndpointFailed(failed, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);

  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, authority.port);
  CPPUNIT_ASSERT(authority.directOrigin);
  CPPUNIT_ASSERT(!request->hasHttpsServiceBindingEndpointInfo());
  CPPUNIT_ASSERT(e.findCachedIPAddress("svc.example", 8443).empty());
}

void HttpInitiateConnectionCommandTest::
    testSelectConnectionAuthorityUsesProxyWithCachedSvcbEndpoint()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_TRUE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  auto proxyRequest = makeRequest("http://proxy.example:8080/");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord());
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(proxyRequest);

  CPPUNIT_ASSERT_EQUAL(std::string("proxy.example"), authority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8080, authority.port);
  CPPUNIT_ASSERT(!authority.directOrigin);
  CPPUNIT_ASSERT(!request->hasHttpsServiceBindingEndpointInfo());
  CPPUNIT_ASSERT(e.findCachedIPAddress("svc.example", 8443).empty());
}

void HttpInitiateConnectionCommandTest::
    testCreateNextCommandReusesSvcbConnectPortPooledSocket()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  auto sockets = createSocketPair();
  auto peerInfo = sockets.first->getPeerInfo();
  const uint16_t connectPort = peerInfo.port == 8443 ? 8444 : 8443;
  e.poolSocket("192.0.2.10", connectPort, A2STR::NIL, 0, sockets.first,
               std::chrono::seconds(60));
  std::vector<std::string> resolvedAddresses;
  resolvedAddresses.push_back("192.0.2.10");

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto nextCommand =
      command.createNextCommand("svc.example", "192.0.2.10", connectPort,
                                resolvedAddresses, nullptr);

  CPPUNIT_ASSERT(nextCommand);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example"),
                       request->getConnectedHostname());
  CPPUNIT_ASSERT_EQUAL(peerInfo.addr, request->getConnectedAddr());
  CPPUNIT_ASSERT_EQUAL(peerInfo.port, request->getConnectedPort());
}

void HttpInitiateConnectionCommandTest::
    testConnectFailureMarksSvcbEndpointFailed()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_ASYNC_DNS, A2_V_TRUE);
  option->put(PREF_DISABLE_IPV6, A2_V_FALSE);
  option->put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto requestGroup =
      std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = makeRequest("https://origin.example/file");
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(option.get());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(makeSvcbRecord("svc1.example", 8443, 1, "192.0.2.10"));
  records.push_back(makeSvcbRecord("svc2.example", 9443, 2, "192.0.2.11"));
  e.cacheHttpsServiceBindingRecords("origin.example", 443, records, 60);

  TestHttpInitiateConnectionCommand command(request, requestGroup, &e);
  auto authority = command.selectConnectionAuthority(nullptr);
  CPPUNIT_ASSERT_EQUAL(std::string("svc1.example"), authority.hostname);
  CPPUNIT_ASSERT(request->hasHttpsServiceBindingEndpointInfo());

  command.onAllConnectAddressesFailed("svc1.example", 8443);

  CPPUNIT_ASSERT(!request->hasHttpsServiceBindingEndpointInfo());
  auto failed = makeEndpoint("origin.example", 443, "svc1.example", 8443);
  failed.alpn = "http/1.1";
  CPPUNIT_ASSERT(e.isHttpsServiceBindingEndpointFailed(failed));

  auto nextAuthority = command.selectConnectionAuthority(nullptr);
  CPPUNIT_ASSERT_EQUAL(std::string("svc2.example"), nextAuthority.hostname);
  CPPUNIT_ASSERT_EQUAL((uint16_t)9443, nextAuthority.port);
}

} // namespace aria2
