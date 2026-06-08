#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2ResponseAdapter.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>

#  include "DlAbortEx.h"
#  include "Http2Session.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"

namespace aria2 {

class Http2ResponseAdapterTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2ResponseAdapterTest);
  CPPUNIT_TEST(testCreateHttpResponseFromHttp2Event);
  CPPUNIT_TEST(testFiltersPseudoAndUnknownHeaders);
  CPPUNIT_TEST(testFiltersConnectionSpecificHeaders);
  CPPUNIT_TEST(testBodyDoesNotSynthesizeContentLength);
  CPPUNIT_TEST(testRejectIncompleteOrMalformedResponseEvent);
  CPPUNIT_TEST_SUITE_END();

public:
  void testCreateHttpResponseFromHttp2Event();
  void testFiltersPseudoAndUnknownHeaders();
  void testFiltersConnectionSpecificHeaders();
  void testBodyDoesNotSynthesizeContentLength();
  void testRejectIncompleteOrMalformedResponseEvent();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2ResponseAdapterTest);

void Http2ResponseAdapterTest::testCreateHttpResponseFromHttp2Event()
{
  Http2ResponseEvent event;
  event.headersComplete = true;
  event.status = 206;
  event.headers.emplace_back("content-length", " 42 ");
  event.headers.emplace_back("content-range", "bytes 0-41/100");
  event.headers.emplace_back("content-type", "text/plain; charset=UTF-8");
  event.headers.emplace_back("location", "https://example.org/next");
  event.headers.emplace_back("set-cookie", "a=1");
  event.headers.emplace_back("set-cookie", "b=2");

  auto response = createHttpResponseFromHttp2Event(event);

  CPPUNIT_ASSERT(response);
  CPPUNIT_ASSERT(response->getHttpHeader());
  CPPUNIT_ASSERT_EQUAL(206, response->getStatusCode());
  CPPUNIT_ASSERT_EQUAL(std::string("HTTP/2"),
                       response->getHttpHeader()->getVersion());
  CPPUNIT_ASSERT_EQUAL((int64_t)42LL, response->getContentLength());
  CPPUNIT_ASSERT_EQUAL(std::string("text/plain"), response->getContentType());
  CPPUNIT_ASSERT_EQUAL(std::string("https://example.org/next"),
                       response->getRedirectURI());
  auto cookies = response->getHttpHeader()->findAll(HttpHeader::SET_COOKIE);
  CPPUNIT_ASSERT_EQUAL((size_t)2, cookies.size());
  CPPUNIT_ASSERT_EQUAL(std::string("a=1"), cookies[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("b=2"), cookies[1]);
}

void Http2ResponseAdapterTest::testFiltersPseudoAndUnknownHeaders()
{
  Http2ResponseEvent event;
  event.headersComplete = true;
  event.status = 200;
  event.headers.emplace_back(":status", "404");
  event.headers.emplace_back("x-test", "ignored");
  event.headers.emplace_back("content-type", "application/octet-stream");

  auto response = createHttpResponseFromHttp2Event(event);

  CPPUNIT_ASSERT_EQUAL(200, response->getStatusCode());
  CPPUNIT_ASSERT_EQUAL(std::string("application/octet-stream"),
                       response->getContentType());
  CPPUNIT_ASSERT(!response->getHttpHeader()->defined(HttpHeader::LOCATION));
}

void Http2ResponseAdapterTest::testFiltersConnectionSpecificHeaders()
{
  Http2ResponseEvent event;
  event.headersComplete = true;
  event.status = 200;
  event.headers.emplace_back("content-length", "200");
  event.headers.emplace_back("content-range", "bytes 0-199/300");
  event.headers.emplace_back("transfer-encoding", "chunked");

  auto response = createHttpResponseFromHttp2Event(event);

  CPPUNIT_ASSERT(!response->isTransferEncodingSpecified());
  CPPUNIT_ASSERT_EQUAL((int64_t)200LL, response->getContentLength());
  CPPUNIT_ASSERT_EQUAL((int64_t)300LL, response->getEntityLength());
}

void Http2ResponseAdapterTest::testBodyDoesNotSynthesizeContentLength()
{
  Http2ResponseEvent event;
  event.headersComplete = true;
  event.status = 200;
  CPPUNIT_ASSERT(event.body.push(
      reinterpret_cast<const unsigned char*>("hello"), 5));

  auto response = createHttpResponseFromHttp2Event(event);

  CPPUNIT_ASSERT(!response->getHttpHeader()->defined(
      HttpHeader::CONTENT_LENGTH));
  CPPUNIT_ASSERT_EQUAL((int64_t)0LL, response->getContentLength());
}

void Http2ResponseAdapterTest::testRejectIncompleteOrMalformedResponseEvent()
{
  Http2ResponseEvent event;
  event.status = 200;
  CPPUNIT_ASSERT_THROW(createHttpResponseFromHttp2Event(event), DlAbortEx);

  event.headersComplete = true;
  event.status = 0;
  CPPUNIT_ASSERT_THROW(createHttpResponseFromHttp2Event(event), DlAbortEx);

  event.status = 1000;
  CPPUNIT_ASSERT_THROW(createHttpResponseFromHttp2Event(event), DlAbortEx);
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
