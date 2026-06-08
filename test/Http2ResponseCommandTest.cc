#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2ResponseCommand.h"

#  include <memory>
#  include <string>
#  include <utility>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "AuthConfigFactory.h"
#  include "DlAbortEx.h"
#  include "DownloadContext.h"
#  include "DownloadEngine.h"
#  include "FileEntry.h"
#  include "Http2DownloadCommand.h"
#  include "Http2SingleStreamExchange.h"
#  include "Http2TestUtil.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "Option.h"
#  include "Request.h"
#  include "RequestGroup.h"
#  include "RequestGroupMan.h"
#  include "SelectEventPoll.h"
#  include "StreamFilter.h"
#  include "TestUtil.h"
#  include "prefs.h"
#  include "util.h"

namespace aria2 {

class Http2ResponseCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2ResponseCommandTest);
  CPPUNIT_TEST(testWaitsForHeaders);
  CPPUNIT_TEST(testZeroLengthResponseCompletes);
  CPPUNIT_TEST(testBodyDownloadCommandCreationDoesNotThrow);
  CPPUNIT_TEST(testDownloadCommandCompletesKnownLengthBody);
  CPPUNIT_TEST(testDownloadCommandWaitsForEndStreamAfterBody);
  CPPUNIT_TEST(testDownloadCommandCompletesBodyAcrossSegments);
  CPPUNIT_TEST(testDownloadCommandWaitsForEndStreamAcrossSegments);
  CPPUNIT_TEST(testDownloadCommandAbortsBodyLongerThanFile);
  CPPUNIT_TEST(testDownloadCommandAbortsClosedBeforeComplete);
  CPPUNIT_TEST(testSkipBodyRedirectsAfterEndStream);
  CPPUNIT_TEST(testSkipHeadRedirectsWithContentLength);
  CPPUNIT_TEST(testSkipBodyAborts404AfterEndStream);
  CPPUNIT_TEST(testSkipBodyAbortsBodyLongerThanContentLength);
  CPPUNIT_TEST(testSkipBodyRedirectsWhenBodyShorterThanContentLength);
  CPPUNIT_TEST(testSkipBodyAborts404WhenBodyShorterThanContentLength);
  CPPUNIT_TEST(testSkipBodyAbortsOnStreamError);
  CPPUNIT_TEST(testTransportFailureThrows);
  CPPUNIT_TEST(testStreamClosedBeforeHeadersThrows);
  CPPUNIT_TEST_SUITE_END();

public:
  void testWaitsForHeaders();
  void testZeroLengthResponseCompletes();
  void testBodyDownloadCommandCreationDoesNotThrow();
  void testDownloadCommandCompletesKnownLengthBody();
  void testDownloadCommandWaitsForEndStreamAfterBody();
  void testDownloadCommandCompletesBodyAcrossSegments();
  void testDownloadCommandWaitsForEndStreamAcrossSegments();
  void testDownloadCommandAbortsBodyLongerThanFile();
  void testDownloadCommandAbortsClosedBeforeComplete();
  void testSkipBodyRedirectsAfterEndStream();
  void testSkipHeadRedirectsWithContentLength();
  void testSkipBodyAborts404AfterEndStream();
  void testSkipBodyAbortsBodyLongerThanContentLength();
  void testSkipBodyRedirectsWhenBodyShorterThanContentLength();
  void testSkipBodyAborts404WhenBodyShorterThanContentLength();
  void testSkipBodyAbortsOnStreamError();
  void testTransportFailureThrows();
  void testStreamClosedBeforeHeadersThrows();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2ResponseCommandTest);

namespace {

class TestHttp2ResponseCommand : public Http2ResponseCommand {
private:
  bool requeued_ = false;

public:
  TestHttp2ResponseCommand(
      cuid_t cuid, const std::shared_ptr<Request>& req,
      const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
      std::shared_ptr<Http2SingleStreamExchange> exchange,
      std::unique_ptr<HttpRequest> httpRequest, DownloadEngine* e,
      const std::shared_ptr<SocketCore>& s)
      : Http2ResponseCommand(cuid, req, fileEntry, requestGroup,
                             std::move(exchange), std::move(httpRequest), e, s)
  {
  }

  using Http2ResponseCommand::executeInternal;

  bool requeued() const { return requeued_; }

protected:
  void requeueSelf() CXX11_OVERRIDE { requeued_ = true; }
};

class TestHttp2DownloadCommand : public Http2DownloadCommand {
private:
  bool requeued_ = false;

public:
  TestHttp2DownloadCommand(
      cuid_t cuid, const std::shared_ptr<Request>& req,
      const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
      std::shared_ptr<Http2SingleStreamExchange> exchange,
      std::unique_ptr<HttpResponse> httpResponse, DownloadEngine* e,
      const std::shared_ptr<SocketCore>& s)
      : Http2DownloadCommand(cuid, req, fileEntry, requestGroup,
                             std::move(exchange), std::move(httpResponse),
                             std::unique_ptr<StreamFilter>{}, e, s)
  {
  }

  using Http2DownloadCommand::executeInternal;

  bool requeued() const { return requeued_; }

protected:
  void requeueSelf() CXX11_OVERRIDE { requeued_ = true; }
};

struct CommandFixture {
  std::shared_ptr<Option> option;
  std::shared_ptr<RequestGroup> requestGroup;
  DownloadEngine engine;
  RequestGroupMan* requestGroupMan;
  std::shared_ptr<Request> request;
  std::shared_ptr<FileEntry> fileEntry;
  AuthConfigFactory authConfigFactory;
  http2test::MemoryHttp2Transport transport;
  std::shared_ptr<Http2SingleStreamExchange> exchange;
  http2test::FakeHttp2ServerSession server;
  int32_t streamId;

  CommandFixture(int64_t totalLength = 0, bool dryRun = true,
                 bool initPieceStorage = false, int32_t pieceLength = 1_m)
      : option(makeOption(dryRun, pieceLength)), requestGroup(createRequestGroup(
                                      pieceLength, totalLength, "file.bin",
                                      "https://origin.example/file.bin",
                                      option)),
        engine(make_unique<SelectEventPoll>()),
        requestGroupMan(nullptr),
        request(makeRequest()),
        fileEntry(requestGroup->getDownloadContext()->getFirstFileEntry()),
        exchange(std::make_shared<Http2SingleStreamExchange>(transport)),
        streamId(0)
  {
    engine.setOption(option.get());
    auto rgman = make_unique<RequestGroupMan>(
        std::vector<std::shared_ptr<RequestGroup>>{}, 1, option.get());
    requestGroupMan = rgman.get();
    engine.setRequestGroupMan(std::move(rgman));
    requestGroup->setRequestGroupMan(requestGroupMan);
    fileEntry->poolRequest(request);
    if (initPieceStorage) {
      requestGroup->initPieceStorage();
    }

    auto httpRequest = makeHttpRequest();
    streamId = exchange->submitRequest(*httpRequest);
    CPPUNIT_ASSERT(exchange->flushOutboundData());
    server.feedInboundData(transport.drainOutboundData());
  }

  static std::shared_ptr<Option> makeOption(bool dryRun, int32_t pieceLength)
  {
    auto option = std::make_shared<Option>();
    option->put(PREF_DIR, ".");
    option->put(PREF_DRY_RUN, dryRun ? A2_V_TRUE : A2_V_FALSE);
    option->put(PREF_FILE_ALLOCATION, V_NONE);
    option->put(PREF_PIECE_LENGTH, util::itos(pieceLength));
    option->put(PREF_SPLIT, "1");
    option->put(PREF_TIMEOUT, "60");
    option->put(PREF_MAX_DOWNLOAD_LIMIT, "0");
    option->put(PREF_MAX_OVERALL_DOWNLOAD_LIMIT, "0");
    option->put(PREF_MAX_HTTP_PIPELINING, "1");
    option->put(PREF_HTTP_ACCEPT_GZIP, A2_V_FALSE);
    option->put(PREF_MAX_FILE_NOT_FOUND, "0");
    option->put(PREF_NO_NETRC, A2_V_TRUE);
    option->put(PREF_RETRY_WAIT, "0");
    option->put(PREF_CONTENT_DISPOSITION_DEFAULT_UTF8, A2_V_FALSE);
    option->put(PREF_REMOTE_TIME, A2_V_FALSE);
    option->put(PREF_SELECT_LEAST_USED_HOST, A2_V_FALSE);
    option->put(PREF_STARTUP_IDLE_TIME, "0");
    option->put(PREF_LOWEST_SPEED_LIMIT, "0");
    option->put(PREF_FOLLOW_TORRENT, A2_V_FALSE);
    option->put(PREF_FOLLOW_METALINK, A2_V_FALSE);
    return option;
  }

  static std::shared_ptr<Request> makeRequest()
  {
    auto request = std::make_shared<Request>();
    CPPUNIT_ASSERT(request->setUri("https://origin.example/file.bin"));
    request->setMethod(Request::METHOD_GET);
    request->supportsPersistentConnection(true);
    return request;
  }

  std::unique_ptr<HttpRequest> makeHttpRequest()
  {
    auto httpRequest = make_unique<HttpRequest>();
    httpRequest->disableContentEncoding();
    httpRequest->setRequest(request);
    httpRequest->setFileEntry(fileEntry);
    httpRequest->setAuthConfigFactory(&authConfigFactory);
    httpRequest->setOption(option.get());
    httpRequest->setCookieStorage(engine.getCookieStorage().get());
    httpRequest->setNoWantDigest(true);
    httpRequest->createRequest();
    return httpRequest;
  }

  std::unique_ptr<TestHttp2ResponseCommand> makeCommand()
  {
    return make_unique<TestHttp2ResponseCommand>(
        1, request, fileEntry, requestGroup.get(), exchange, makeHttpRequest(),
        &engine, nullptr);
  }

  std::unique_ptr<TestHttp2DownloadCommand> makeDownloadCommand(
      std::unique_ptr<HttpResponse> httpResponse)
  {
    return make_unique<TestHttp2DownloadCommand>(
        1, request, fileEntry, requestGroup.get(), exchange,
        std::move(httpResponse), &engine, nullptr);
  }

  void submitResponseHeaders(Http2HeaderBlock headers)
  {
    server.submitResponseHeaders(streamId, headers);
    transport.appendInboundData(server.drainOutboundData());
  }

  std::unique_ptr<HttpResponse> receiveResponse(Http2HeaderBlock headers,
                                                const std::string& body)
  {
    server.submitResponse(streamId, headers, body);
    transport.appendInboundData(server.drainOutboundData());
    CPPUNIT_ASSERT(exchange->pump());
    auto httpResponse = exchange->createHttpResponse();
    CPPUNIT_ASSERT(httpResponse);
    httpResponse->setCuid(1);
    httpResponse->setHttpRequest(makeHttpRequest());
    return httpResponse;
  }

  std::unique_ptr<HttpResponse> receiveResponseHeaders(Http2HeaderBlock headers)
  {
    submitResponseHeaders(std::move(headers));
    CPPUNIT_ASSERT(exchange->pump());
    auto httpResponse = exchange->createHttpResponse();
    CPPUNIT_ASSERT(httpResponse);
    httpResponse->setCuid(1);
    httpResponse->setHttpRequest(makeHttpRequest());
    return httpResponse;
  }
};

Http2HeaderBlock createHeaders(int statusCode, int64_t contentLength = -1)
{
  Http2HeaderBlock headers;
  headers.emplace_back(":status", util::uitos(statusCode));
  if (contentLength >= 0) {
    headers.emplace_back("content-length", util::uitos(contentLength));
  }
  return headers;
}

} // namespace

void Http2ResponseCommandTest::testWaitsForHeaders()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
}

void Http2ResponseCommandTest::testZeroLengthResponseCompletes()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(200, 0));

  CPPUNIT_ASSERT(command->executeInternal());
}

void Http2ResponseCommandTest::testBodyDownloadCommandCreationDoesNotThrow()
{
  CommandFixture fixture(4, false, true);
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(200, 4));

  CPPUNIT_ASSERT(command->executeInternal());
}

void Http2ResponseCommandTest::testDownloadCommandCompletesKnownLengthBody()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponse(createHeaders(200, 4), "body");
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));

  CPPUNIT_ASSERT(command->execute());
  CPPUNIT_ASSERT(fixture.requestGroup->downloadFinished());
}

void Http2ResponseCommandTest::testDownloadCommandWaitsForEndStreamAfterBody()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponseHeaders(createHeaders(200, 4));
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->execute());
  CPPUNIT_ASSERT(command->requeued());
  CPPUNIT_ASSERT(!fixture.requestGroup->downloadFinished());

  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->execute());
  CPPUNIT_ASSERT(fixture.requestGroup->downloadFinished());
}

void Http2ResponseCommandTest::testDownloadCommandCompletesBodyAcrossSegments()
{
  CommandFixture fixture(6, false, true, 3);
  auto httpResponse = fixture.receiveResponse(createHeaders(200, 6), "abcdef");
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));

  CPPUNIT_ASSERT(command->execute());
  CPPUNIT_ASSERT(fixture.requestGroup->downloadFinished());
}

void Http2ResponseCommandTest::testDownloadCommandWaitsForEndStreamAcrossSegments()
{
  CommandFixture fixture(6, false, true, 3);
  auto httpResponse = fixture.receiveResponseHeaders(createHeaders(200, 6));
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "abcdef");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->execute());
  CPPUNIT_ASSERT(command->requeued());
  CPPUNIT_ASSERT(!fixture.requestGroup->downloadFinished());

  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->execute());
  CPPUNIT_ASSERT(fixture.requestGroup->downloadFinished());
}

void Http2ResponseCommandTest::testDownloadCommandAbortsBodyLongerThanFile()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponse(createHeaders(200), "body!");
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));

  CPPUNIT_ASSERT_THROW(command->execute(), DlAbortEx);
}

void Http2ResponseCommandTest::testDownloadCommandAbortsClosedBeforeComplete()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponse(createHeaders(200, 4), "");
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));

  CPPUNIT_ASSERT_THROW(command->execute(), DlAbortEx);
}

void Http2ResponseCommandTest::testSkipBodyRedirectsAfterEndStream()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 4);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
  CPPUNIT_ASSERT_EQUAL(std::string("https://origin.example/file.bin"),
                       fixture.request->getCurrentUri());

  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->executeInternal());
  CPPUNIT_ASSERT_EQUAL(std::string("https://origin.example/next"),
                       fixture.request->getCurrentUri());
}

void Http2ResponseCommandTest::testSkipHeadRedirectsWithContentLength()
{
  CommandFixture fixture;
  fixture.request->setMethod(Request::METHOD_HEAD);
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 5);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));
  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->executeInternal());
  CPPUNIT_ASSERT_EQUAL(std::string("https://origin.example/next"),
                       fixture.request->getCurrentUri());
}

void Http2ResponseCommandTest::testSkipBodyAborts404AfterEndStream()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(404, 4));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());

  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

void Http2ResponseCommandTest::testSkipBodyAbortsBodyLongerThanContentLength()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 4);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body!");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

void Http2ResponseCommandTest::testSkipBodyRedirectsWhenBodyShorterThanContentLength()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 5);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->executeInternal());
  CPPUNIT_ASSERT_EQUAL(std::string("https://origin.example/next"),
                       fixture.request->getCurrentUri());
}

void Http2ResponseCommandTest::testSkipBodyAborts404WhenBodyShorterThanContentLength()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(404, 5));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

void Http2ResponseCommandTest::testSkipBodyAbortsOnStreamError()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 0);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));
  fixture.server.submitRstStream(fixture.streamId, NGHTTP2_CANCEL);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

void Http2ResponseCommandTest::testTransportFailureThrows()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.transport.setFailRead(true);

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

void Http2ResponseCommandTest::testStreamClosedBeforeHeadersThrows()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.server.submitRstStream(fixture.streamId, NGHTTP2_CANCEL);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT_THROW(command->executeInternal(), DlAbortEx);
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
