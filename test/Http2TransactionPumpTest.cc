#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2TransactionPump.h"

#  include <algorithm>
#  include <cstring>
#  include <limits>
#  include <string>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "DlAbortEx.h"
#  include "Http2TestUtil.h"
#  include "Http2Transaction.h"
#  include "Http2Transport.h"
#  include "HttpResponse.h"

namespace aria2 {

class MemoryHttp2Transport : public Http2Transport {
private:
  std::string inbound_;
  std::string outbound_;
  size_t maxWriteSize_;
  size_t maxReadSize_;
  bool blockAfterWrite_;
  bool writeBlocked_;
  bool failWrite_;
  bool failRead_;
  bool closed_;
  bool wantRead_;
  bool wantWrite_;

public:
  MemoryHttp2Transport()
      : maxWriteSize_(std::numeric_limits<size_t>::max()),
        maxReadSize_(std::numeric_limits<size_t>::max()),
        blockAfterWrite_(false),
        writeBlocked_(false),
        failWrite_(false),
        failRead_(false),
        closed_(false),
        wantRead_(false),
        wantWrite_(false)
  {
  }

  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE
  {
    wantRead_ = false;
    wantWrite_ = false;
    if (failWrite_) {
      return -1;
    }
    if (closed_) {
      return 0;
    }
    if (writeBlocked_) {
      writeBlocked_ = false;
      wantWrite_ = true;
      return 0;
    }

    auto nwrite = std::min(len, maxWriteSize_);
    outbound_.append(static_cast<const char*>(data), nwrite);
    if (blockAfterWrite_) {
      writeBlocked_ = true;
    }
    return static_cast<ssize_t>(nwrite);
  }

  virtual ssize_t readData(void* data, size_t len) CXX11_OVERRIDE
  {
    wantRead_ = false;
    wantWrite_ = false;
    if (failRead_) {
      return -1;
    }
    if (inbound_.empty()) {
      if (!closed_) {
        wantRead_ = true;
      }
      return 0;
    }

    auto nread = std::min(len, inbound_.size());
    nread = std::min(nread, maxReadSize_);
    std::memcpy(data, inbound_.data(), nread);
    inbound_.erase(0, nread);
    return static_cast<ssize_t>(nread);
  }

  virtual size_t getRecvBufferedLength() const CXX11_OVERRIDE
  {
    return inbound_.size();
  }

  virtual bool wantRead() const CXX11_OVERRIDE { return wantRead_; }

  virtual bool wantWrite() const CXX11_OVERRIDE { return wantWrite_; }

  void appendInboundData(const std::string& data) { inbound_.append(data); }

  std::string drainOutboundData()
  {
    std::string data;
    data.swap(outbound_);
    return data;
  }

  void setMaxWriteSize(size_t size) { maxWriteSize_ = size; }

  void setMaxReadSize(size_t size) { maxReadSize_ = size; }

  void setBlockAfterWrite(bool f) { blockAfterWrite_ = f; }

  void setFailWrite(bool f) { failWrite_ = f; }

  void setFailRead(bool f) { failRead_ = f; }

  void close() { closed_ = true; }
};

class Http2TransactionPumpTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2TransactionPumpTest);
  CPPUNIT_TEST(testFlushOutboundWritesRequest);
  CPPUNIT_TEST(testFlushOutboundHandlesPartialWrite);
  CPPUNIT_TEST(testPumpFeedsInboundResponse);
  CPPUNIT_TEST(testPumpHandlesPartialRead);
  CPPUNIT_TEST(testWriteFailureThrows);
  CPPUNIT_TEST(testReadFailureThrows);
  CPPUNIT_TEST(testClosedReadThrows);
  CPPUNIT_TEST_SUITE_END();

public:
  void testFlushOutboundWritesRequest();
  void testFlushOutboundHandlesPartialWrite();
  void testPumpFeedsInboundResponse();
  void testPumpHandlesPartialRead();
  void testWriteFailureThrows();
  void testReadFailureThrows();
  void testClosedReadThrows();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2TransactionPumpTest);

void Http2TransactionPumpTest::testFlushOutboundWritesRequest()
{
  Http2Transaction transaction;
  MemoryHttp2Transport transport;
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
  MemoryHttp2Transport transport;
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
  MemoryHttp2Transport transport;
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
  MemoryHttp2Transport transport;
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

void Http2TransactionPumpTest::testWriteFailureThrows()
{
  Http2Transaction transaction;
  MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transaction.submitRequest(http2test::createRequestHeaders());
  transport.setFailWrite(true);

  CPPUNIT_ASSERT_THROW(pump.flushOutboundData(), DlAbortEx);
  CPPUNIT_ASSERT(pump.hasPendingOutboundData());
}

void Http2TransactionPumpTest::testReadFailureThrows()
{
  Http2Transaction transaction;
  MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transport.setFailRead(true);

  CPPUNIT_ASSERT_THROW(pump.readInboundData(), DlAbortEx);
}

void Http2TransactionPumpTest::testClosedReadThrows()
{
  Http2Transaction transaction;
  MemoryHttp2Transport transport;
  Http2TransactionPump pump(transaction, transport);

  transport.close();

  CPPUNIT_ASSERT_THROW(pump.readInboundData(), DlAbortEx);
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
