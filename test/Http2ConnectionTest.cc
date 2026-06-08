#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Connection.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "DlAbortEx.h"
#  include "Http2TestUtil.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"

namespace aria2 {

class Http2ConnectionTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2ConnectionTest);
  CPPUNIT_TEST(testSubmitRequestDrainsOutboundData);
  CPPUNIT_TEST(testFeedInboundDataFindsResponseEvent);
  CPPUNIT_TEST(testPopResponseBodyKeepsEvent);
  CPPUNIT_TEST(testPopResponseEventRemovesEvent);
  CPPUNIT_TEST(testPopHttpResponseConsumesEvent);
  CPPUNIT_TEST(testPopHttpResponseWaitsForStreamClose);
  CPPUNIT_TEST(testPopHttpResponseKeepsMalformedEvent);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitRequestDrainsOutboundData();
  void testFeedInboundDataFindsResponseEvent();
  void testPopResponseBodyKeepsEvent();
  void testPopResponseEventRemovesEvent();
  void testPopHttpResponseConsumesEvent();
  void testPopHttpResponseWaitsForStreamClose();
  void testPopHttpResponseKeepsMalformedEvent();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2ConnectionTest);

void Http2ConnectionTest::testSubmitRequestDrainsOutboundData()
{
  Http2Connection connection;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());
  auto data = connection.drainOutboundData();

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT_EQUAL((int32_t)1, streamId % 2);
  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT_EQUAL(std::string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"),
                       data.substr(0, 24));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_SETTINGS));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_HEADERS));
  CPPUNIT_ASSERT(connection.drainOutboundData().empty());
}

void Http2ConnectionTest::testFeedInboundDataFindsResponseEvent()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "");
  connection.feedInboundData(server.drainOutboundData());

  auto response = connection.findResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT(connection.hasResponseEvent(streamId));
  CPPUNIT_ASSERT_EQUAL(streamId, response->streamId);
  CPPUNIT_ASSERT_EQUAL(200, response->status);
  CPPUNIT_ASSERT(response->headersComplete);
  CPPUNIT_ASSERT_EQUAL(std::string("text/plain"),
                       http2test::findHeader(response->headers,
                                             "content-type"));
  CPPUNIT_ASSERT(response->streamClosed);
  CPPUNIT_ASSERT_EQUAL((uint32_t)NGHTTP2_NO_ERROR, response->errorCode);
}

void Http2ConnectionTest::testPopResponseBodyKeepsEvent()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "hello");
  connection.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT_EQUAL(std::string("hel"),
                       connection.popResponseBody(streamId, 3));
  CPPUNIT_ASSERT(connection.hasResponseEvent(streamId));

  auto response = connection.popResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(std::string("lo"), response->body.drainAll());
}

void Http2ConnectionTest::testPopResponseEventRemovesEvent()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponse(streamId, http2test::createResponseHeaders(), "hello");
  connection.feedInboundData(server.drainOutboundData());

  auto response = connection.popResponseEvent(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(std::string("hello"), response->body.drainAll());
  CPPUNIT_ASSERT(!connection.hasResponseEvent(streamId));
  CPPUNIT_ASSERT(!connection.popResponseEvent(streamId));
}

void Http2ConnectionTest::testPopHttpResponseConsumesEvent()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());
  auto responseHeaders = http2test::createResponseHeaders();
  responseHeaders.emplace_back("content-length", "123");

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponse(streamId, responseHeaders, "body");
  connection.feedInboundData(server.drainOutboundData());

  auto response = connection.popHttpResponse(streamId);
  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT_EQUAL(200, response->getStatusCode());
  CPPUNIT_ASSERT_EQUAL(std::string("HTTP/2"),
                       response->getHttpHeader()->getVersion());
  CPPUNIT_ASSERT_EQUAL(std::string("text/plain"), response->getContentType());
  CPPUNIT_ASSERT_EQUAL((int64_t)123LL, response->getContentLength());
  CPPUNIT_ASSERT(!connection.hasResponseEvent(streamId));
  CPPUNIT_ASSERT(!connection.popHttpResponse(streamId));
}

void Http2ConnectionTest::testPopHttpResponseWaitsForStreamClose()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponseHeaders(streamId, http2test::createResponseHeaders());
  connection.feedInboundData(server.drainOutboundData());

  auto event = connection.findResponseEvent(streamId);
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT(event->headersComplete);
  CPPUNIT_ASSERT(!event->streamClosed);
  CPPUNIT_ASSERT(!connection.popHttpResponse(streamId));
  CPPUNIT_ASSERT(connection.hasResponseEvent(streamId));
}

void Http2ConnectionTest::testPopHttpResponseKeepsMalformedEvent()
{
  Http2Connection connection;
  http2test::FakeHttp2ServerSession server;
  auto streamId = connection.submitRequest(http2test::createRequestHeaders());
  Http2HeaderBlock responseHeaders;
  responseHeaders.emplace_back(":status", "0");

  server.feedInboundData(connection.drainOutboundData());
  server.submitResponse(streamId, responseHeaders, "");
  connection.feedInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(connection.hasResponseEvent(streamId));
  CPPUNIT_ASSERT_THROW(connection.popHttpResponse(streamId), DlAbortEx);
  CPPUNIT_ASSERT(connection.hasResponseEvent(streamId));
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
