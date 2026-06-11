#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Session.h"
#  include "Http2TestUtil.h"

#  include <cstdint>
#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "a2functional.h"

namespace aria2 {

class Http2SessionTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2SessionTest);
  CPPUNIT_TEST(testSubmitRequestHeadersProducesClientBytes);
  CPPUNIT_TEST(testSubmitRequestAdvertisesLargeReceiveWindow);
  CPPUNIT_TEST(testSubmitRequestWithBodyProducesDataFrame);
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
  void testSubmitRequestAdvertisesLargeReceiveWindow();
  void testSubmitRequestWithBodyProducesDataFrame();
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

namespace {
bool findFramePayload(const std::string& data, unsigned char frameType,
                      int32_t streamId, std::string& payload,
                      uint8_t& flags)
{
  size_t offset = 0;
  if (data.size() >= 24 &&
      data.substr(0, 24) == "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") {
    offset = 24;
  }

  while (offset + 9 <= data.size()) {
    auto length = (static_cast<unsigned char>(data[offset]) << 16) |
                  (static_cast<unsigned char>(data[offset + 1]) << 8) |
                  static_cast<unsigned char>(data[offset + 2]);
    auto type = static_cast<unsigned char>(data[offset + 3]);
    flags = static_cast<uint8_t>(data[offset + 4]);
    auto sid = ((static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 5])) << 24) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 6])) << 16) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 7])) << 8) |
                static_cast<uint32_t>(
                    static_cast<unsigned char>(data[offset + 8]))) &
               0x7fffffffu;
    if (offset + 9 + length > data.size()) {
      return false;
    }
    if (type == frameType && static_cast<int32_t>(sid) == streamId) {
      payload.assign(data, offset + 9, length);
      return true;
    }
    offset += 9 + length;
  }
  return false;
}

bool findSettingsValue(const std::string& data, uint16_t id, uint32_t& value)
{
  std::string payload;
  uint8_t flags = 0;
  if (!findFramePayload(data, NGHTTP2_SETTINGS, 0, payload, flags) ||
      (flags & NGHTTP2_FLAG_ACK)) {
    return false;
  }
  for (size_t i = 0; i + 6 <= payload.size(); i += 6) {
    auto entryId =
        (static_cast<uint16_t>(static_cast<unsigned char>(payload[i])) << 8) |
        static_cast<uint16_t>(static_cast<unsigned char>(payload[i + 1]));
    if (entryId == id) {
      value =
          (static_cast<uint32_t>(
               static_cast<unsigned char>(payload[i + 2])) << 24) |
          (static_cast<uint32_t>(
               static_cast<unsigned char>(payload[i + 3])) << 16) |
          (static_cast<uint32_t>(
               static_cast<unsigned char>(payload[i + 4])) << 8) |
          static_cast<uint32_t>(static_cast<unsigned char>(payload[i + 5]));
      return true;
    }
  }
  return false;
}

bool findConnectionWindowUpdate(const std::string& data, uint32_t& increment)
{
  std::string payload;
  uint8_t flags = 0;
  if (!findFramePayload(data, NGHTTP2_WINDOW_UPDATE, 0, payload, flags) ||
      payload.size() != 4) {
    return false;
  }
  increment =
      ((static_cast<uint32_t>(static_cast<unsigned char>(payload[0])) << 24) |
       (static_cast<uint32_t>(static_cast<unsigned char>(payload[1])) << 16) |
       (static_cast<uint32_t>(static_cast<unsigned char>(payload[2])) << 8) |
       static_cast<uint32_t>(static_cast<unsigned char>(payload[3]))) &
      0x7fffffffu;
  return true;
}
} // namespace

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

void Http2SessionTest::testSubmitRequestAdvertisesLargeReceiveWindow()
{
  Http2Session session;
  session.submitRequestHeaders(http2test::createRequestHeaders());
  auto data = session.drainOutboundData();
  uint32_t initialWindowSize = 0;
  uint32_t connectionWindowUpdate = 0;

  CPPUNIT_ASSERT(findSettingsValue(data, NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                                   initialWindowSize));
  CPPUNIT_ASSERT(initialWindowSize >= static_cast<uint32_t>(1_m));
  CPPUNIT_ASSERT(findConnectionWindowUpdate(data, connectionWindowUpdate));
  CPPUNIT_ASSERT(connectionWindowUpdate >= static_cast<uint32_t>(15_m));
}

void Http2SessionTest::testSubmitRequestWithBodyProducesDataFrame()
{
  Http2Session session;
  auto headers = http2test::createRequestHeaders();
  headers[0].value = "POST";
  headers.emplace_back("content-type", "application/dns-message");
  headers.emplace_back("content-length", "5");

  auto streamId = session.submitRequest(headers, "query");
  auto data = session.drainOutboundData();
  std::string payload;
  uint8_t flags = 0;

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_DATA));
  CPPUNIT_ASSERT(findFramePayload(data, NGHTTP2_DATA, streamId, payload,
                                  flags));
  CPPUNIT_ASSERT_EQUAL(std::string("query"), payload);
  CPPUNIT_ASSERT(flags & NGHTTP2_FLAG_END_STREAM);
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
