#include "HttpSkipResponseCommand.h"

#include <cppunit/extensions/HelperMacros.h>

#include "FileEntry.h"
#include "HttpHeader.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Piece.h"
#include "PiecedSegment.h"
#include "Request.h"

namespace aria2 {

class HttpSkipResponseCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpSkipResponseCommandTest);
  CPPUNIT_TEST(testShouldRetryHttpStatusByDefault);
  CPPUNIT_TEST(testShouldRedirectHttpStatusWithLocation);
  CPPUNIT_TEST_SUITE_END();

public:
  void testShouldRetryHttpStatusByDefault();
  void testShouldRedirectHttpStatusWithLocation();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpSkipResponseCommandTest);

void HttpSkipResponseCommandTest::testShouldRetryHttpStatusByDefault()
{
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(200));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(302));

  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(400));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(405));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(407));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(410));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(429));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(500));

  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(401));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(403));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(404));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(416));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(502));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(503));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(504));
}

namespace {

std::unique_ptr<HttpResponse> createResponse(int statusCode,
                                             const char* location,
                                             size_t pieceIndex,
                                             bool pipelining,
                                             int64_t endOffsetOverride,
                                             const std::string& method)
{
  auto httpHeader = make_unique<HttpHeader>();
  httpHeader->setStatusCode(statusCode);
  if (location) {
    httpHeader->put(HttpHeader::LOCATION, location);
  }

  auto request = std::make_shared<Request>();
  request->setUri("http://example.org/download");
  request->supportsPersistentConnection(true);
  request->setPipeliningHint(pipelining);
  request->setMethod(method);

  auto piece = std::make_shared<Piece>(pieceIndex, 1_m);
  auto segment = std::make_shared<PiecedSegment>(1_m, piece);
  auto fileEntry = std::make_shared<FileEntry>("file", 10_m, 0);

  auto httpRequest = make_unique<HttpRequest>();
  httpRequest->setRequest(request);
  httpRequest->setSegment(segment);
  httpRequest->setFileEntry(fileEntry);
  httpRequest->setEndOffsetOverride(endOffsetOverride);

  auto httpResponse = make_unique<HttpResponse>();
  httpResponse->setHttpHeader(std::move(httpHeader));
  httpResponse->setHttpRequest(std::move(httpRequest));
  return httpResponse;
}

std::unique_ptr<HttpResponse> createResponseWithoutSegment()
{
  auto httpHeader = make_unique<HttpHeader>();
  httpHeader->setStatusCode(416);
  httpHeader->put(HttpHeader::LOCATION, "http://bucket.example.org/object");

  auto request = std::make_shared<Request>();
  request->setUri("http://example.org/download");

  auto httpRequest = make_unique<HttpRequest>();
  httpRequest->setRequest(request);

  auto httpResponse = make_unique<HttpResponse>();
  httpResponse->setHttpHeader(std::move(httpHeader));
  httpResponse->setHttpRequest(std::move(httpRequest));
  return httpResponse;
}

} // namespace

void HttpSkipResponseCommandTest::testShouldRedirectHttpStatusWithLocation()
{
  HttpResponse emptyResponse;

  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(emptyResponse));

  CPPUNIT_ASSERT(shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "http://bucket.example.org/object", 1, false, 0,
                      Request::METHOD_GET)));
  CPPUNIT_ASSERT(shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "http://bucket.example.org/object", 0, true, 0,
                      Request::METHOD_GET)));
  CPPUNIT_ASSERT(shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "http://bucket.example.org/object", 0, false, 1_m,
                      Request::METHOD_GET)));

  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponse(416, nullptr, 1, false, 0, Request::METHOD_GET)));
  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "", 1, false, 0, Request::METHOD_GET)));
  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponse(302, "http://bucket.example.org/object", 1, false, 0,
                      Request::METHOD_GET)));
  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "http://bucket.example.org/object", 0, false, 0,
                      Request::METHOD_GET)));
  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponse(416, "http://bucket.example.org/object", 1, false, 0,
                      Request::METHOD_HEAD)));
  CPPUNIT_ASSERT(!shouldRedirectHttpStatusWithLocation(
      *createResponseWithoutSegment()));
}

} // namespace aria2
