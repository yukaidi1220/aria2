#include "Http2HeaderBlock.h"

#include <string>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "DlAbortEx.h"

namespace aria2 {

class Http2HeaderBlockTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2HeaderBlockTest);
  CPPUNIT_TEST(testCreateHttp2HeaderBlockFromHttp1Request);
  CPPUNIT_TEST(testCreateHttp2HeaderBlockFromAbsoluteFormRequest);
  CPPUNIT_TEST(testFilterConnectionSpecificHeaders);
  CPPUNIT_TEST(testTeTrailersIsAllowed);
  CPPUNIT_TEST(testRejectMalformedRequest);
  CPPUNIT_TEST_SUITE_END();

public:
  void testCreateHttp2HeaderBlockFromHttp1Request();
  void testCreateHttp2HeaderBlockFromAbsoluteFormRequest();
  void testFilterConnectionSpecificHeaders();
  void testTeTrailersIsAllowed();
  void testRejectMalformedRequest();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2HeaderBlockTest);

namespace {
const Http2Header& nth(const Http2HeaderBlock& block, size_t index)
{
  CPPUNIT_ASSERT(index < block.size());
  return block[index];
}

bool hasHeader(const Http2HeaderBlock& block, const std::string& name,
               const std::string& value)
{
  for (const auto& header : block) {
    if (header.name == name && header.value == value) {
      return true;
    }
  }
  return false;
}
} // namespace

void Http2HeaderBlockTest::testCreateHttp2HeaderBlockFromHttp1Request()
{
  auto block = createHttp2HeaderBlockFromHttp1Request(
      "GET /archives/file.tar.gz?mirror=1 HTTP/1.1\r\n"
      "User-Agent: aria2\r\n"
      "Accept: */*\r\n"
      "Host: origin.example:8443\r\n"
      "Range: bytes=0-1023\r\n"
      "\r\n",
      "https");

  CPPUNIT_ASSERT_EQUAL((size_t)7, block.size());
  CPPUNIT_ASSERT_EQUAL(std::string(":method"), nth(block, 0).name);
  CPPUNIT_ASSERT_EQUAL(std::string("GET"), nth(block, 0).value);
  CPPUNIT_ASSERT_EQUAL(std::string(":scheme"), nth(block, 1).name);
  CPPUNIT_ASSERT_EQUAL(std::string("https"), nth(block, 1).value);
  CPPUNIT_ASSERT_EQUAL(std::string(":authority"), nth(block, 2).name);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example:8443"),
                       nth(block, 2).value);
  CPPUNIT_ASSERT_EQUAL(std::string(":path"), nth(block, 3).name);
  CPPUNIT_ASSERT_EQUAL(std::string("/archives/file.tar.gz?mirror=1"),
                       nth(block, 3).value);
  CPPUNIT_ASSERT(hasHeader(block, "user-agent", "aria2"));
  CPPUNIT_ASSERT(hasHeader(block, "accept", "*/*"));
  CPPUNIT_ASSERT(hasHeader(block, "range", "bytes=0-1023"));
}

void Http2HeaderBlockTest::testCreateHttp2HeaderBlockFromAbsoluteFormRequest()
{
  auto block = createHttp2HeaderBlockFromHttp1Request(
      "GET https://origin.example/download.bin?x=1 HTTP/1.1\r\n"
      "Host: stale.example\r\n"
      "\r\n",
      "http");

  CPPUNIT_ASSERT_EQUAL(std::string(":scheme"), nth(block, 1).name);
  CPPUNIT_ASSERT_EQUAL(std::string("https"), nth(block, 1).value);
  CPPUNIT_ASSERT_EQUAL(std::string(":authority"), nth(block, 2).name);
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), nth(block, 2).value);
  CPPUNIT_ASSERT_EQUAL(std::string(":path"), nth(block, 3).name);
  CPPUNIT_ASSERT_EQUAL(std::string("/download.bin?x=1"), nth(block, 3).value);

  block = createHttp2HeaderBlockFromHttp1Request(
      "GET https://user:password@origin.example?x=1 HTTP/1.1\r\n"
      "Host: stale.example\r\n"
      "\r\n",
      "https");
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), nth(block, 2).value);
  CPPUNIT_ASSERT_EQUAL(std::string("/?x=1"), nth(block, 3).value);
}

void Http2HeaderBlockTest::testFilterConnectionSpecificHeaders()
{
  auto block = createHttp2HeaderBlockFromHttp1Request(
      "GET / HTTP/1.1\r\n"
      "Host: example.org\r\n"
      "Connection: keep-alive, X-Hop\r\n"
      "Keep-Alive: timeout=5\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Upgrade: websocket\r\n"
      "X-Hop: remove-me\r\n"
      "X-End-To-End: keep-me\r\n"
      "\r\n",
      "https");

  CPPUNIT_ASSERT(!hasHeader(block, "connection", "keep-alive, X-Hop"));
  CPPUNIT_ASSERT(!hasHeader(block, "keep-alive", "timeout=5"));
  CPPUNIT_ASSERT(!hasHeader(block, "proxy-connection", "keep-alive"));
  CPPUNIT_ASSERT(!hasHeader(block, "transfer-encoding", "chunked"));
  CPPUNIT_ASSERT(!hasHeader(block, "upgrade", "websocket"));
  CPPUNIT_ASSERT(!hasHeader(block, "x-hop", "remove-me"));
  CPPUNIT_ASSERT(hasHeader(block, "x-end-to-end", "keep-me"));
}

void Http2HeaderBlockTest::testTeTrailersIsAllowed()
{
  auto block = createHttp2HeaderBlockFromHttp1Request(
      "GET / HTTP/1.1\r\n"
      "Host: example.org\r\n"
      "TE: Trailers\r\n"
      "TE: gzip\r\n"
      "TE: trailers, gzip\r\n"
      "\r\n",
      "https");

  CPPUNIT_ASSERT(hasHeader(block, "te", "trailers"));
  CPPUNIT_ASSERT(!hasHeader(block, "te", "gzip"));
  CPPUNIT_ASSERT(!hasHeader(block, "te", "trailers, gzip"));
}

void Http2HeaderBlockTest::testRejectMalformedRequest()
{
  CPPUNIT_ASSERT_THROW(
      createHttp2HeaderBlockFromHttp1Request("", "https"), DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "GET / HTTP/1.1\r\n"
                           "User-Agent: aria2\r\n"
                           "\r\n",
                           "https"),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "GET / HTTP/1.1\r\n"
                           "Host: example.org\r\n"
                           "Broken-Header\r\n"
                           "\r\n",
                           "https"),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "GET / HTTP/1.1\r\n"
                           "Host: example.org\r\n"
                           "Bad Header: value\r\n"
                           "\r\n",
                           "https"),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "CONNECT example.org:443 HTTP/1.1\r\n"
                           "Host: example.org:443\r\n"
                           "\r\n",
                           "https"),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "GET / HTTP/1.1\r\n"
                           "Host: example.org\r\n"
                           "\r\n",
                           ""),
                       DlAbortEx);
  CPPUNIT_ASSERT_THROW(createHttp2HeaderBlockFromHttp1Request(
                           "GET https:///path HTTP/1.1\r\n"
                           "Host: example.org\r\n"
                           "\r\n",
                           "https"),
                       DlAbortEx);
}

} // namespace aria2
