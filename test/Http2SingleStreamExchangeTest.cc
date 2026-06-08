#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2SingleStreamExchange.h"

#  include <memory>
#  include <string>
#  include <utility>

#  include <cppunit/extensions/HelperMacros.h>

#  include "AuthConfigFactory.h"
#  include "DlAbortEx.h"
#  include "Http2TestUtil.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "Option.h"
#  include "Request.h"
#  include "a2functional.h"

namespace aria2 {

class Http2SingleStreamExchangeTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2SingleStreamExchangeTest);
  CPPUNIT_TEST(testSubmitPumpHeadersAndDrainBody);
  CPPUNIT_TEST(testSubmitRequestMakesExchangeWantWrite);
  CPPUNIT_TEST(testOwnedTransportSubmitAndFlush);
  CPPUNIT_TEST(testWantReadAndWriteReflectTransportState);
  CPPUNIT_TEST(testTransportFailuresThrow);
  CPPUNIT_TEST(testRejectSecondRequestWhileActive);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitPumpHeadersAndDrainBody();
  void testSubmitRequestMakesExchangeWantWrite();
  void testOwnedTransportSubmitAndFlush();
  void testWantReadAndWriteReflectTransportState();
  void testTransportFailuresThrow();
  void testRejectSecondRequestWhileActive();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2SingleStreamExchangeTest);

namespace {
void configureRequest(HttpRequest& httpRequest,
                      const std::shared_ptr<Request>& request, Option* option,
                      AuthConfigFactory* authConfigFactory)
{
  httpRequest.disableContentEncoding();
  httpRequest.setRequest(request);
  httpRequest.setAuthConfigFactory(authConfigFactory);
  httpRequest.setOption(option);
  httpRequest.setNoWantDigest(true);
}
} // namespace

void Http2SingleStreamExchangeTest::testSubmitPumpHeadersAndDrainBody()
{
  http2test::MemoryHttp2Transport transport;
  Http2SingleStreamExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  auto streamId = exchange.submitRequest(httpRequest);
  CPPUNIT_ASSERT(exchange.hasActiveStream());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());

  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", "4");
  server.submitResponseHeaders(streamId, headers);
  transport.appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange.pump());

  auto state = exchange.getState();
  CPPUNIT_ASSERT(state.headersComplete);
  CPPUNIT_ASSERT(!state.streamClosed);
  auto response = exchange.createHttpResponse();
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL((int64_t)4LL, response->getContentLength());

  server.submitResponseData(streamId, "body");
  transport.appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange.pump());
  CPPUNIT_ASSERT_EQUAL(std::string("bo"), exchange.popResponseBody(2));
  CPPUNIT_ASSERT_EQUAL(std::string("dy"), exchange.popResponseBody(16));

  state = exchange.getState();
  CPPUNIT_ASSERT(state.streamClosed);
  CPPUNIT_ASSERT_EQUAL((size_t)0, state.bodyLength);
  auto event = exchange.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT(!exchange.hasActiveStream());
}

void Http2SingleStreamExchangeTest::testSubmitRequestMakesExchangeWantWrite()
{
  http2test::MemoryHttp2Transport transport;
  Http2SingleStreamExchange exchange(transport);
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  CPPUNIT_ASSERT(!exchange.wantWrite());
  auto streamId = exchange.submitRequest(httpRequest);

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT(exchange.wantWrite());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(!transport.drainOutboundData().empty());
  CPPUNIT_ASSERT(!exchange.wantWrite());
}

void Http2SingleStreamExchangeTest::testOwnedTransportSubmitAndFlush()
{
  auto transport = make_unique<http2test::MemoryHttp2Transport>();
  auto rawTransport = transport.get();
  Http2SingleStreamExchange exchange(std::move(transport));
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  exchange.submitRequest(httpRequest);
  CPPUNIT_ASSERT(exchange.hasActiveStream());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(!rawTransport->drainOutboundData().empty());
}

void Http2SingleStreamExchangeTest::testWantReadAndWriteReflectTransportState()
{
  http2test::MemoryHttp2Transport transport;
  Http2SingleStreamExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  transport.setMaxWriteSize(7);
  transport.setBlockAfterWrite(true);
  auto streamId = exchange.submitRequest(httpRequest);
  std::string outboundData;
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(exchange.wantWrite());
  outboundData += transport.drainOutboundData();

  size_t iterations = 0;
  while (exchange.wantWrite()) {
    CPPUNIT_ASSERT(iterations++ < 100);
    CPPUNIT_ASSERT(exchange.flushOutboundData());
    outboundData += transport.drainOutboundData();
  }
  server.feedInboundData(outboundData);

  server.submitResponseHeaders(streamId, http2test::createResponseHeaders());
  transport.appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange.wantRead());
  CPPUNIT_ASSERT(exchange.readInboundData());
}

void Http2SingleStreamExchangeTest::testTransportFailuresThrow()
{
  http2test::MemoryHttp2Transport transport;
  Http2SingleStreamExchange exchange(transport);
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  exchange.submitRequest(httpRequest);
  transport.setFailWrite(true);
  CPPUNIT_ASSERT_THROW(exchange.flushOutboundData(), DlAbortEx);

  transport.setFailWrite(false);
  transport.setFailRead(true);
  CPPUNIT_ASSERT_THROW(exchange.readInboundData(), DlAbortEx);
}

void Http2SingleStreamExchangeTest::testRejectSecondRequestWhileActive()
{
  http2test::MemoryHttp2Transport transport;
  Http2SingleStreamExchange exchange(transport);
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, request, &option, &authConfigFactory);

  exchange.submitRequest(httpRequest);
  CPPUNIT_ASSERT_THROW(exchange.submitRequest(httpRequest), DlAbortEx);
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
