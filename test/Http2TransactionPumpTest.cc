#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2TransactionPump.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "DlAbortEx.h"
#  include "a2functional.h"
#  include "Http2TestUtil.h"
#  include "Http2Transaction.h"
#  include "Http2Transport.h"
#  include "HttpResponse.h"

namespace aria2 {

class Http2TransactionPumpTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2TransactionPumpTest);
  CPPUNIT_TEST(testFlushOutboundWritesRequest);
  CPPUNIT_TEST(testFlushOutboundHandlesPartialWrite);
  CPPUNIT_TEST(testPumpFeedsInboundResponse);
  CPPUNIT_TEST(testPumpHandlesPartialRead);
  CPPUNIT_TEST(testPumpDrainsBufferedReads);
  CPPUNIT_TEST(testPumpDrainsSocketUntilWouldBlockWithoutBufferedHint);
  CPPUNIT_TEST(testPumpReadsLargeResponseInBatches);
  CPPUNIT_TEST(testPumpPausesBeforeBodyQueueOverflow);
  CPPUNIT_TEST(testWriteFailureThrows);
  CPPUNIT_TEST(testReadFailureThrows);
  CPPUNIT_TEST(testClosedReadThrows);
  CPPUNIT_TEST_SUITE_END();

public:
  void testFlushOutboundWritesRequest();
  void testFlushOutboundHandlesPartialWrite();
  void testPumpFeedsInboundResponse();
  void testPumpHandlesPartialRead();
  void testPumpDrainsBufferedReads();
  void testPumpDrainsSocketUntilWouldBlockWithoutBufferedHint();
  void testPumpReadsLargeResponseInBatches();
  void testPumpPausesBeforeBodyQueueOverflow();
  void testWriteFailureThrows();
  void testReadFailureThrows();
  void testClosedReadThrows();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2TransactionPumpTest);

void Http2TransactionPumpTest::testFlushOutboundWritesRequest()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transaction.submitRequest(http2test::createRequestHeaders());
  CPPUNIT_ASSERT(pump.flushOutboundData());

  auto data = transport.drainOutboundData();
  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_SETTINGS));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_HEADERS));
  CPPUNIT_ASSERT(!pump.hasPendingOutboundData());
  CPPUNIT_ASSERT(!pump.flushOutboundData());
}

void Http2TransactionPumpTest::testFlushOutboundHandlesPartialWrite()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  std::string data;

  transport.setMaxWriteSize(7);
  transport.setBlockAfterWrite(true);
  transaction.submitRequest(http2test::createRequestHeaders());

  CPPUNIT_ASSERT(pump.flushOutboundData());
  data += transport.drainOutboundData();
  CPPUNIT_ASSERT(pump.hasPendingOutboundData());
  CPPUNIT_ASSERT(pump.wantWrite());

  size_t iterations = 0;
  while (pump.hasPendingOutboundData()) {
    CPPUNIT_ASSERT(iterations++ < 100);
    CPPUNIT_ASSERT(pump.flushOutboundData());
    data += transport.drainOutboundData();
  }

  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_SETTINGS));
  CPPUNIT_ASSERT(http2test::containsFrameType(data, NGHTTP2_HEADERS));
}

void Http2TransactionPumpTest::testPumpFeedsInboundResponse()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", "4");

  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(streamId, headers, "body");
  transport.appendInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(pump.wantRead());
  CPPUNIT_ASSERT(pump.pump());

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(std::string("body"), event->body.drainAll());
  CPPUNIT_ASSERT(event->body.closed());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionPumpTest::testPumpHandlesPartialRead()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", "12");

  transport.setMaxReadSize(3);
  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(streamId, headers, "chunked-body");
  transport.appendInboundData(server.drainOutboundData());

  size_t iterations = 0;
  while (transport.getRecvBufferedLength() > 0) {
    CPPUNIT_ASSERT(iterations++ < 100);
    CPPUNIT_ASSERT(pump.pump());
  }

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(std::string("chunked-body"), event->body.drainAll());
  CPPUNIT_ASSERT(event->body.closed());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionPumpTest::testPumpDrainsBufferedReads()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", "12");

  transport.setMaxReadSize(16);
  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(streamId, headers, "chunked-body");
  transport.appendInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(transport.getRecvBufferedLength() > 0);
  CPPUNIT_ASSERT(pump.pump());
  CPPUNIT_ASSERT_EQUAL((size_t)0, transport.getRecvBufferedLength());

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(std::string("chunked-body"), event->body.drainAll());
  CPPUNIT_ASSERT(event->body.closed());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionPumpTest::
    testPumpDrainsSocketUntilWouldBlockWithoutBufferedHint()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  std::string body(256_k, 'x');
  headers.emplace_back("content-length", "262144");

  transport.setReportBufferedLength(false);
  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(streamId, headers, body);
  transport.appendInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT_EQUAL((size_t)0, transport.getRecvBufferedLength());
  CPPUNIT_ASSERT(pump.pump());

  auto state = transaction.getState();
  CPPUNIT_ASSERT_EQUAL(body.size(), state.bodyLength);
  CPPUNIT_ASSERT(transport.getReadCount() > 1);

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(body.size(), event->body.drainAll().size());
  CPPUNIT_ASSERT(event->body.closed());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionPumpTest::testPumpReadsLargeResponseInBatches()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  std::string body(256_k, 'x');
  headers.emplace_back("content-length", "262144");

  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponse(streamId, headers, body);
  transport.appendInboundData(server.drainOutboundData());

  CPPUNIT_ASSERT(transport.getRecvBufferedLength() > 16_k);
  CPPUNIT_ASSERT(pump.pump());
  CPPUNIT_ASSERT_EQUAL((size_t)0, transport.getRecvBufferedLength());
  CPPUNIT_ASSERT(transport.getReadCount() <= 8);

  auto event = transaction.popResponseEvent();
  CPPUNIT_ASSERT(event);
  CPPUNIT_ASSERT_EQUAL(body.size(), event->body.drainAll().size());
  CPPUNIT_ASSERT(event->body.closed());
  CPPUNIT_ASSERT(!transaction.hasActiveStream());
}

void Http2TransactionPumpTest::testPumpPausesBeforeBodyQueueOverflow()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);
  http2test::FakeHttp2ServerSession server;
  auto streamId = transaction.submitRequest(http2test::createRequestHeaders());
  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", "10485760");
  std::string chunk(64_k, 'x');

  CPPUNIT_ASSERT(pump.flushOutboundData());
  server.feedInboundData(transport.drainOutboundData());
  server.submitResponseHeaders(streamId, headers);
  for (size_t i = 0; i < 160; ++i) {
    server.submitResponseDataNoEndStream(streamId, chunk);
  }
  transport.appendInboundData(server.drainOutboundData());

  size_t iterations = 0;
  while (pump.wantRead() && transport.getRecvBufferedLength() > 0) {
    CPPUNIT_ASSERT(iterations++ < 100);
    pump.pump();
  }

  auto state = transaction.getState();
  CPPUNIT_ASSERT(state.bodyLength > 0);
  CPPUNIT_ASSERT(state.bodyLength <= 8_m);
  CPPUNIT_ASSERT(transport.getRecvBufferedLength() > 0);
  CPPUNIT_ASSERT(!pump.wantRead());
  CPPUNIT_ASSERT(!pump.hasBufferedInboundData());

  auto body = transaction.popResponseBody(1_m);
  CPPUNIT_ASSERT_EQUAL((size_t)1_m, body.size());
  CPPUNIT_ASSERT(pump.wantRead());
  CPPUNIT_ASSERT(pump.hasBufferedInboundData());
  CPPUNIT_ASSERT(pump.pump());

  auto stateAfterResume = transaction.getState();
  CPPUNIT_ASSERT(stateAfterResume.bodyLength > state.bodyLength - body.size());
}

void Http2TransactionPumpTest::testWriteFailureThrows()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transaction.submitRequest(http2test::createRequestHeaders());
  transport.setFailWrite(true);

  CPPUNIT_ASSERT_THROW(pump.flushOutboundData(), DlAbortEx);
  CPPUNIT_ASSERT(pump.hasPendingOutboundData());
}

void Http2TransactionPumpTest::testReadFailureThrows()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transport.setFailRead(true);

  CPPUNIT_ASSERT_THROW(pump.readInboundData(), DlAbortEx);
}

void Http2TransactionPumpTest::testClosedReadThrows()
{
  Http2Transaction transaction;
  http2test::MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transport.close();

  CPPUNIT_ASSERT_THROW(pump.readInboundData(), DlAbortEx);
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
