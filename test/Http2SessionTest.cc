#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Session.h"
#  include "Http2TestUtil.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

namespace aria2 {

class Http2SessionTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2SessionTest);
  CPPUNIT_TEST(testSubmitRequestHeadersProducesClientBytes);
  CPPUNIT_TEST(testDrainOutboundDataClearsBuffer);
  CPPUNIT_TEST(testFeedInboundDataFlushesSettingsAck);
  CPPUNIT_TEST(testFeedInboundDataUpdatesRemoteMaxConcurrentStreams);
  CPPUNIT_TEST(testFeedInboundDataCollectsResponseHeaders);
  CPPUNIT_TEST(testFeedInboundDataCollectsResponseBodyAndClose);
  CPPUNIT_TEST(testFeedInboundDataAcceptsPartialFrames);
  CPPUNIT_TEST(testPopResponseBodyKeepsEvent);
  CPPUNIT_TEST(testPopResponseEventRemovesEvent);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitRequestHeadersProducesClientBytes();
  void testDrainOutboundDataClearsBuffer();
  void testFeedInboundDataFlushesSettingsAck();
  void testFeedInboundDataUpdatesRemoteMaxConcurrentStreams();
  void testFeedInboundDataCollectsResponseHeaders();
  void testFeedInboundDataCollectsResponseBodyAndClose();
  void testFeedInboundDataAcceptsPartialFrames();
  void testPopResponseBodyKeepsEvent();
  void testPopResponseEventRemovesEvent();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2SessionTest);

void Http2SessionTest::testSubmitRequestHeadersProducesClientBytes()
{
  Http2Session session;
  auto streamId =
      session.submitRequestHeaders(http2test::createRequestHeaders());
  auto data = session.drainOutboundData();

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT_EQUAL((int32_t)1, streamId % 2);
  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT_EQUAL(std::string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"),
                       data.substr(0, 24));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_SETTINGS));
}

void Http2SessionTest::testDrainOutboundDataClearsBuffer()
{
  Http2Session session;
  session.submitRequestHeaders(http2test::createRequestHeaders());
  CPPUNIT_ASSERT(!session.drainOutboundData().empty());
  CPPUNIT_ASSERT(session.drainOutboundData().empty());
}

void Http2SessionTest::testFeedInboundDataFlushesSettingsAck()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;

  server.flushPendingData();
  client.feedInboundData(server.drainOutboundData());

  auto data = client.drainOutboundData();
  CPPUNIT_ASSERT(http2test::containsSettingsAck(data));
}

void Http2SessionTest::testFeedInboundDataUpdatesRemoteMaxConcurrentStreams()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;

  server.submitMaxConcurrentStreams(2);
  client.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT_EQUAL((size_t)2, client.getRemoteMaxConcurrentStreams());
}

void Http2SessionTest::testFeedInboundDataCollectsResponseHeaders()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;
  auto streamId =
      client.submitRequestHeaders(http2test::createRequestHeaders());

  server.feedInboundData(client.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "");
  client.feedInboundData(server.drainOutboundData());

  auto response = client.findResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT(client.hasResponseEvent(streamId));
  CPPUNIT_ASSERT_EQUAL(streamId, response->streamId);
  CPPUNIT_ASSERT_EQUAL(200, response->status);
  CPPUNIT_ASSERT(response->headersComplete);
  CPPUNIT_ASSERT(response->body.closed());
  CPPUNIT_ASSERT_EQUAL(std::string("text/plain"),
                       http2test::findHeader(response->headers,
                                             "content-type"));
  CPPUNIT_ASSERT_EQUAL(std::string("ok"),
                       http2test::findHeader(response->headers, "x-test"));
  CPPUNIT_ASSERT(response->streamClosed);
  CPPUNIT_ASSERT_EQUAL((uint32_t)NGHTTP2_NO_ERROR, response->errorCode);
}

void Http2SessionTest::testFeedInboundDataCollectsResponseBodyAndClose()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;
  auto streamId =
      client.submitRequestHeaders(http2test::createRequestHeaders());

  server.feedInboundData(client.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "hello");
  client.feedInboundData(server.drainOutboundData());

  auto response = client.popResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(std::string("hello"), response->body.drainAll());
  CPPUNIT_ASSERT(response->body.closed());
  CPPUNIT_ASSERT(response->streamClosed);
  CPPUNIT_ASSERT_EQUAL((uint32_t)NGHTTP2_NO_ERROR, response->errorCode);
}

void Http2SessionTest::testFeedInboundDataAcceptsPartialFrames()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;
  auto streamId =
      client.submitRequestHeaders(http2test::createRequestHeaders());

  server.feedInboundData(client.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(),
                        "partial-body");
  http2test::feedInChunks(client, server.drainOutboundData());

  auto response = client.popResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(200, response->status);
  CPPUNIT_ASSERT(response->headersComplete);
  CPPUNIT_ASSERT_EQUAL(std::string("partial-body"),
                       response->body.drainAll());
  CPPUNIT_ASSERT(response->streamClosed);
  CPPUNIT_ASSERT_EQUAL((uint32_t)NGHTTP2_NO_ERROR, response->errorCode);
}

void Http2SessionTest::testPopResponseBodyKeepsEvent()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;
  auto streamId =
      client.submitRequestHeaders(http2test::createRequestHeaders());

  server.feedInboundData(client.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "hello");
  client.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT_EQUAL(std::string("he"), client.popResponseBody(streamId, 2));
  auto response = client.findResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL((size_t)3, response->body.size());

  auto popped = client.popResponseEvent(streamId);
  CPPUNIT_ASSERT(popped);
  CPPUNIT_ASSERT_EQUAL(std::string("llo"), popped->body.drainAll());
}

void Http2SessionTest::testPopResponseEventRemovesEvent()
{
  Http2Session client;
  http2test::FakeHttp2ServerSession server;
  auto streamId =
      client.submitRequestHeaders(http2test::createRequestHeaders());

  server.feedInboundData(client.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "hello");
  client.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(client.hasResponseEvent(streamId));
  auto response = client.popResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(streamId, response->streamId);
  CPPUNIT_ASSERT_EQUAL(200, response->status);
  CPPUNIT_ASSERT_EQUAL(std::string("hello"), response->body.drainAll());
  CPPUNIT_ASSERT(!client.hasResponseEvent(streamId));
  CPPUNIT_ASSERT(!client.popResponseEvent(streamId));
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
