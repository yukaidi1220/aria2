#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Transaction.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "DlAbortEx.h"
#  include "Http2TestUtil.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"

namespace aria2 {

class Http2TransactionTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2TransactionTest);
  CPPUNIT_TEST(testSubmitRequestStoresSingleStreamId);
  CPPUNIT_TEST(testGetStateTracksResponseProgress);
  CPPUNIT_TEST(testGetStateReflectsBodyDrainAndPop);
  CPPUNIT_TEST(testDrainOutboundDataAfterSubmit);
  CPPUNIT_TEST(testFeedInboundDataPopHttpResponse);
  CPPUNIT_TEST(testPopResponseBodyKeepsActiveStream);
  CPPUNIT_TEST(testPopResponseEventDrainsBody);
  CPPUNIT_TEST(testPopResponseEventKeepsActiveUntilStreamClose);
  CPPUNIT_TEST(testPopHttpResponseKeepsActiveUntilStreamClose);
  CPPUNIT_TEST(testRejectSecondSubmitWhileStreamActive);
  CPPUNIT_TEST(testMalformedResponseKeepsActiveStream);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitRequestStoresSingleStreamId();
  void testGetStateTracksResponseProgress();
  void testGetStateReflectsBodyDrainAndPop();
  void testDrainOutboundDataAfterSubmit();
  void testFeedInboundDataPopHttpResponse();
  void testPopResponseBodyKeepsActiveStream();
  void testPopResponseEventDrainsBody();
  void testPopResponseEventKeepsActiveUntilStreamClose();
  void testPopHttpResponseKeepsActiveUntilStreamClose();
  void testRejectSecondSubmitWhileStreamActive();
  void testMalformedResponseKeepsActiveStream();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2TransactionTest);

void Http2TransactionTest::testSubmitRequestStoresSingleStreamId()
{
  Http2Transaction transaction;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT_EQUAL(streamId, transaction.getStreamId());
}

void Http2TransactionTest::testGetStateTracksResponseProgress()
{
  Http2Transaction transaction;
  auto state = transaction.getState();
  CPPUNIT_ASSERT(!state.active);
  CPPUNIT_ASSERT(!state.responseAvailable);

  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  state = transaction.getState();
  CPPUNIT_ASSERT(state.active);
  CPPUNIT_ASSERT(!state.responseAvailable);
  CPPUNIT_ASSERT_EQUAL((size_t)0, state.bodyLength);

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponseHeaders(streamId, http2test::createResponseHeaders());
  transaction.feedInboundData(server.drainOutboundData());
  state = transaction.getState();
  CPPUNIT_ASSERT(state.active);
  CPPUNIT_ASSERT(state.responseAvailable);
  CPPUNIT_ASSERT(state.headersComplete);
  CPPUNIT_ASSERT(!state.streamClosed);
  CPPUNIT_ASSERT_EQUAL((size_t)0, state.bodyLength);

  server.submitResponseData(streamId, "body");
  transaction.feedInboundData(server.drainOutboundData());
  state = transaction.getState();
  CPPUNIT_ASSERT(state.active);
  CPPUNIT_ASSERT(state.streamClosed);
  CPPUNIT_ASSERT_EQUAL((size_t)4, state.bodyLength);
  CPPUNIT_ASSERT_EQUAL((uint32_t)NGHTTP2_NO_ERROR, state.errorCode);
}

void Http2TransactionTest::testGetStateReflectsBodyDrainAndPop()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "body");
  transaction.feedInboundData(server.drainOutboundData());

  auto state = transaction.getState();
  CPPUNIT_ASSERT(state.active);
  CPPUNIT_ASSERT_EQUAL((size_t)4, state.bodyLength);

  CPPUNIT_ASSERT_EQUAL(std::string("bo"), transaction.popResponseBody(2));
  state = transaction.getState();
  CPPUNIT_ASSERT(state.active);
  CPPUNIT_ASSERT_EQUAL((size_t)2, state.bodyLength);

  CPPUNIT_ASSERT(transaction.popResponseEvent());
  state = transaction.getState();
  CPPUNIT_ASSERT(!state.active);
  CPPUNIT_ASSERT(!state.responseAvailable);
  CPPUNIT_ASSERT_EQUAL((size_t)0, state.bodyLength);
}

void Http2TransactionTest::testDrainOutboundDataAfterSubmit()
{
  Http2Transaction transaction;
  transaction.submitRequest(http2test::createRequestHeaders());

  auto data = transaction.drainOutboundData();

  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_SETTINGS));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_HEADERS));
  CPPUNIT_ASSERT(transaction.drainOutboundData().empty());
}

void Http2TransactionTest::testFeedInboundDataPopHttpResponse()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto responseHeaders = http2test::createResponseHeaders();
  responseHeaders.emplace_back("content-length", "321");

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponse(streamId, responseHeaders, "body");
  transaction.feedInboundData(server.drainOutboundData());

  auto event = transaction.findResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(streamId, event->streamId);

  auto response = transaction.popHttpResponse();
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(200, response->getStatusCode());
  CPPUNIT_ASSERT_EQUAL((int64_t)321LL, response->getContentLength());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
  CPPUNIT_ASSERT(!transaction.findResponseEvent());
  CPPUNIT_ASSERT(!transaction.popHttpResponse());
}

void Http2TransactionTest::testPopResponseBodyKeepsActiveStream()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "body");
  transaction.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT_EQUAL(std::string("bo"), transaction.popResponseBody(2));
  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT(transaction.findResponseEvent());

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(std::string("dy"), event->body.drainAll());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionTest::testPopResponseEventDrainsBody()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "body");
  transaction.feedInboundData(server.drainOutboundData());

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(streamId, event->streamId);
  CPPUNIT_ASSERT_EQUAL(std::string("body"), event->body.drainAll());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
  CPPUNIT_ASSERT(!transaction.findResponseEvent());
  CPPUNIT_ASSERT(!transaction.popResponseEvent());
}

void Http2TransactionTest::testPopResponseEventKeepsActiveUntilStreamClose()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponseHeaders(streamId, http2test::createResponseHeaders());
  transaction.feedInboundData(server.drainOutboundData());

  auto event = transaction.findResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT(event->headersComplete);
  CPPUNIT_ASSERT(!event->streamClosed);
  CPPUNIT_ASSERT(!transaction.popResponseEvent());
  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT_THROW(
      transaction.submitRequest(http2test::createRequestHeaders()), DlAbortEx);
}

void Http2TransactionTest::testPopHttpResponseKeepsActiveUntilStreamClose()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponseHeaders(streamId, http2test::createResponseHeaders());
  transaction.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(!transaction.popHttpResponse());
  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT(transaction.findResponseEvent());
  CPPUNIT_ASSERT_THROW(
      transaction.submitRequest(http2test::createRequestHeaders()), DlAbortEx);
}

void Http2TransactionTest::testRejectSecondSubmitWhileStreamActive()
{
  Http2Transaction transaction;
  transaction.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT_THROW(
      transaction.submitRequest(http2test::createRequestHeaders()), DlAbortEx);
}

void Http2TransactionTest::testMalformedResponseKeepsActiveStream()
{
  Http2Transaction transaction;
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  Http2HeaderBlock responseHeaders;
  responseHeaders.emplace_back(":status", "0");

  server.feedInboundData(transaction.drainOutboundData());
  server.submitResponse(streamId, responseHeaders, "");
  transaction.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT_THROW(transaction.popHttpResponse(), DlAbortEx);
  CPPUNIT_ASSERT(transaction.hasActiveStream());
  CPPUNIT_ASSERT(transaction.findResponseEvent());
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
