#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2MultiplexExchange.h"

#  include <memory>
#  include <string>
#  include <utility>

#  include <cppunit/extensions/HelperMacros.h>

#  include "AuthConfigFactory.h"
#  include "Http2TestUtil.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "Option.h"
#  include "Request.h"
#  include "a2functional.h"

namespace aria2 {

class Http2MultiplexExchangeTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2MultiplexExchangeTest);
  CPPUNIT_TEST(testSubmitTwoRequestsAndReadOutOfOrderResponses);
  CPPUNIT_TEST(testSubmitRequestMakesExchangeWantWrite);
  CPPUNIT_TEST(testSubmitRequestAndFlushWritesImmediately);
  CPPUNIT_TEST(testReadInboundDataMakesExchangeWantWrite);
  CPPUNIT_TEST(testReadInboundDataUpdatesRemoteMaxConcurrentStreams);
  CPPUNIT_TEST(testActivateCommandsWakesRegisteredCommands);
  CPPUNIT_TEST(testPopResponseEventKeepsOtherStreamsActive);
  CPPUNIT_TEST(testCreateHttpResponseWaitsPerStream);
  CPPUNIT_TEST(testUnknownStreamIsIgnored);
  CPPUNIT_TEST(testOwnedTransportSubmitAndFlush);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitTwoRequestsAndReadOutOfOrderResponses();
  void testSubmitRequestMakesExchangeWantWrite();
  void testSubmitRequestAndFlushWritesImmediately();
  void testReadInboundDataMakesExchangeWantWrite();
  void testReadInboundDataUpdatesRemoteMaxConcurrentStreams();
  void testActivateCommandsWakesRegisteredCommands();
  void testPopResponseEventKeepsOtherStreamsActive();
  void testCreateHttpResponseWaitsPerStream();
  void testUnknownStreamIsIgnored();
  void testOwnedTransportSubmitAndFlush();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2MultiplexExchangeTest);

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

std::shared_ptr<Request> makeRequest(const std::string& uri)
{
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri(uri));
  return request;
}

void pumpUntilDrained(Http2MultiplexExchange& exchange,
                      http2test::MemoryHttp2Transport& transport)
{
  size_t iterations = 0;
  while (transport.getRecvBufferedLength() > 0 || exchange.wantWrite()) {
    CPPUNIT_ASSERT(iterations++ < 100);
    CPPUNIT_ASSERT(exchange.pump());
  }
}

class NoopCommand : public Command {
public:
  explicit NoopCommand(cuid_t cuid) : Command(cuid) {}

  virtual bool execute() CXX11_OVERRIDE { return false; }
};
} // namespace

void Http2MultiplexExchangeTest::
    testSubmitTwoRequestsAndReadOutOfOrderResponses()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest req1;
  configureRequest(req1, makeRequest("https://origin.example/file.bin"),
                   &option, &authConfigFactory);
  HttpRequest req2;
  configureRequest(req2, makeRequest("https://origin.example/file.bin"),
                   &option, &authConfigFactory);

  auto stream1 = exchange.submitRequest(req1);
  auto stream2 = exchange.submitRequest(req2);
  CPPUNIT_ASSERT(stream2 > stream1);
  CPPUNIT_ASSERT_EQUAL((size_t)2, exchange.countActiveStreams());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());

  auto headers2 = http2test::createResponseHeaders();
  headers2.emplace_back("content-length", "3");
  server.submitResponse(stream2, headers2, "two");
  auto headers1 = http2test::createResponseHeaders();
  headers1.emplace_back("content-length", "3");
  server.submitResponse(stream1, headers1, "one");
  transport.appendInboundData(server.drainOutboundData());

  pumpUntilDrained(exchange, transport);

  auto state1 = exchange.getState(stream1);
  CPPUNIT_ASSERT(state1.active);
  CPPUNIT_ASSERT(state1.headersComplete);
  CPPUNIT_ASSERT(state1.streamClosed);
  CPPUNIT_ASSERT_EQUAL((size_t)3, state1.bodyLength);
  auto state2 = exchange.getState(stream2);
  CPPUNIT_ASSERT(state2.active);
  CPPUNIT_ASSERT(state2.headersComplete);
  CPPUNIT_ASSERT(state2.streamClosed);
  CPPUNIT_ASSERT_EQUAL((size_t)3, state2.bodyLength);

  auto response1 = exchange.createHttpResponse(stream1);
  CPPUNIT_ASSERT(response1);
  CPPUNIT_ASSERT_EQUAL((int64_t)3LL, response1->getContentLength());
  auto response2 = exchange.createHttpResponse(stream2);
  CPPUNIT_ASSERT(response2);
  CPPUNIT_ASSERT_EQUAL((int64_t)3LL, response2->getContentLength());

  CPPUNIT_ASSERT_EQUAL(std::string("one"),
                       exchange.popResponseBody(stream1, 16));
  CPPUNIT_ASSERT_EQUAL(std::string("two"),
                       exchange.popResponseBody(stream2, 16));
}

void Http2MultiplexExchangeTest::testSubmitRequestMakesExchangeWantWrite()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);

  CPPUNIT_ASSERT(!exchange.wantWrite());
  auto stream = exchange.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT(stream > 0);
  CPPUNIT_ASSERT(exchange.wantWrite());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(!transport.drainOutboundData().empty());
  CPPUNIT_ASSERT(!exchange.wantWrite());
}

void Http2MultiplexExchangeTest::testSubmitRequestAndFlushWritesImmediately()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  Option option;
  AuthConfigFactory authConfigFactory;
  HttpRequest httpRequest;
  configureRequest(httpRequest, makeRequest("https://origin.example/file.bin"),
                   &option, &authConfigFactory);

  auto stream = exchange.submitRequestAndFlush(httpRequest);

  CPPUNIT_ASSERT(stream > 0);
  CPPUNIT_ASSERT(exchange.hasActiveStream(stream));
  auto outbound = transport.drainOutboundData();
  CPPUNIT_ASSERT(!outbound.empty());
  CPPUNIT_ASSERT(http2test::containsFrameType(outbound, NGHTTP2_HEADERS));
  CPPUNIT_ASSERT(!exchange.wantWrite());
}

void Http2MultiplexExchangeTest::testReadInboundDataMakesExchangeWantWrite()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;

  transport.appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(!exchange.wantWrite());
  CPPUNIT_ASSERT(exchange.readInboundData());

  CPPUNIT_ASSERT(exchange.wantWrite());
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(!transport.drainOutboundData().empty());
  CPPUNIT_ASSERT(!exchange.wantWrite());
}

void Http2MultiplexExchangeTest::
    testReadInboundDataUpdatesRemoteMaxConcurrentStreams()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;

  server.submitMaxConcurrentStreams(2);
  transport.appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange.readInboundData());

  CPPUNIT_ASSERT_EQUAL((size_t)2, exchange.getRemoteMaxConcurrentStreams());
}

void Http2MultiplexExchangeTest::testActivateCommandsWakesRegisteredCommands()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  NoopCommand command1(1);
  NoopCommand command2(2);

  exchange.registerCommand(&command1);
  exchange.registerCommand(&command2);
  exchange.activateCommands(nullptr);

  CPPUNIT_ASSERT(command1.statusMatch(Command::STATUS_ACTIVE));
  CPPUNIT_ASSERT(command2.statusMatch(Command::STATUS_ACTIVE));

  command1.setStatusInactive();
  command2.setStatusInactive();
  exchange.unregisterCommand(&command1);
  exchange.activateCommands(nullptr);

  CPPUNIT_ASSERT(!command1.statusMatch(Command::STATUS_ACTIVE));
  CPPUNIT_ASSERT(command2.statusMatch(Command::STATUS_ACTIVE));
}

void Http2MultiplexExchangeTest::
    testPopResponseEventKeepsOtherStreamsActive()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;
  auto stream1 = exchange.submitRequest(http2test::createRequestHeaders());
  auto stream2 = exchange.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT(exchange.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(stream1, http2test::createResponseHeaders(), "one");
  server.submitResponseHeaders(stream2, http2test::createResponseHeaders());
  transport.appendInboundData(server.drainOutboundData());
  pumpUntilDrained(exchange, transport);

  CPPUNIT_ASSERT(exchange.getState(stream2).headersComplete);
  CPPUNIT_ASSERT(!exchange.getState(stream2).streamClosed);
  CPPUNIT_ASSERT(!exchange.popResponseEvent(stream2));
  CPPUNIT_ASSERT_EQUAL((size_t)2, exchange.countActiveStreams());

  auto event1 = exchange.popResponseEvent(stream1);
  CPPUNIT_ASSERT(event1);
  CPPUNIT_ASSERT_EQUAL(stream1, event1->streamId);
  CPPUNIT_ASSERT_EQUAL(std::string("one"), event1->body.drainAll());
  CPPUNIT_ASSERT(!exchange.hasActiveStream(stream1));
  CPPUNIT_ASSERT(exchange.hasActiveStream(stream2));
  CPPUNIT_ASSERT_EQUAL((size_t)1, exchange.countActiveStreams());
}

void Http2MultiplexExchangeTest::testCreateHttpResponseWaitsPerStream()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  http2test::FakeHttp2ServerSession server;
  auto stream1 = exchange.submitRequest(http2test::createRequestHeaders());
  auto stream2 = exchange.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT(exchange.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(stream2, http2test::createResponseHeaders(), "two");
  transport.appendInboundData(server.drainOutboundData());
  pumpUntilDrained(exchange, transport);

  CPPUNIT_ASSERT(!exchange.createHttpResponse(stream1));
  auto response2 = exchange.createHttpResponse(stream2);
  CPPUNIT_ASSERT(response2);
  CPPUNIT_ASSERT_EQUAL(200, response2->getStatusCode());
  CPPUNIT_ASSERT(exchange.hasActiveStream(stream1));
  CPPUNIT_ASSERT(exchange.hasActiveStream(stream2));
}

void Http2MultiplexExchangeTest::testUnknownStreamIsIgnored()
{
  http2test::MemoryHttp2Transport transport;
  Http2MultiplexExchange exchange(transport);
  auto state = exchange.getState(99);
  CPPUNIT_ASSERT(!state.active);
  CPPUNIT_ASSERT(!state.responseAvailable);
  CPPUNIT_ASSERT(!exchange.createHttpResponse(99));
  CPPUNIT_ASSERT_EQUAL(std::string(), exchange.popResponseBody(99, 16));
  CPPUNIT_ASSERT(!exchange.popResponseEvent(99));
  CPPUNIT_ASSERT(!exchange.popHttpResponse(99));
}

void Http2MultiplexExchangeTest::testOwnedTransportSubmitAndFlush()
{
  auto transport = make_unique<http2test::MemoryHttp2Transport>();
  auto rawTransport = transport.get();
  Http2MultiplexExchange exchange(std::move(transport));

  auto stream = exchange.submitRequest(http2test::createRequestHeaders());
  CPPUNIT_ASSERT(stream > 0);
  CPPUNIT_ASSERT(exchange.hasActiveStream(stream));
  CPPUNIT_ASSERT(exchange.flushOutboundData());
  CPPUNIT_ASSERT(!rawTransport->drainOutboundData().empty());
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
