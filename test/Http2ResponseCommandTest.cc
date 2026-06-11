#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2ResponseCommand.h"

#  include <chrono>
#  include <memory>
#  include <string>
#  include <utility>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

#  include "AuthConfigFactory.h"
#  include "Command.h"
#  include "DlAbortEx.h"
#  include "DownloadContext.h"
#  include "DownloadEngine.h"
#  include "FileEntry.h"
#  include "Http2DownloadCommand.h"
#  include "Http2MultiplexExchange.h"
#  include "Http2TestUtil.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "Option.h"
#  include "Request.h"
#  include "RequestGroup.h"
#  include "RequestGroupMan.h"
#  include "SelectEventPoll.h"
#  include "Segment.h"
#  include "SegmentMan.h"
#  include "SocketCore.h"
#  include "StreamFilter.h"
#  include "TestUtil.h"
#  include "prefs.h"
#  include "util.h"

namespace aria2 {

class Http2ResponseCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2ResponseCommandTest);
  CPPUNIT_TEST(testResponseCommandStartsActive);
  CPPUNIT_TEST(testRefreshZeroRunsSkippedInactiveCommands);
  CPPUNIT_TEST(testWaitsForHeaders);
  CPPUNIT_TEST(testWaitsForSelectedStreamHeaders);
  CPPUNIT_TEST(testCanSkipConnectionAccounting);
  CPPUNIT_TEST(testZeroLengthResponseCompletes);
  CPPUNIT_TEST(testResponseKeepsSinglePipelinedRequest);
  CPPUNIT_TEST(testBodyDownloadCommandCreationDoesNotThrow);
  CPPUNIT_TEST(testBodyDownloadCommandStartsActive);
  CPPUNIT_TEST(testDownloadCommandCompletesKnownLengthBody);
  CPPUNIT_TEST(testDownloadCommandWaitsForEndStreamAfterBody);
  CPPUNIT_TEST(testDownloadCommandSchedulesAfterBodyProgress);
  CPPUNIT_TEST(testDownloadCommandCompletesBodyAcrossSegments);
  CPPUNIT_TEST(testDownloadCommandWaitsForEndStreamAcrossSegments);
  CPPUNIT_TEST(testDownloadCommandCancelsBufferedBodyAfterRetry);
  CPPUNIT_TEST(testDownloadCommandAbortsBodyLongerThanFile);
  CPPUNIT_TEST(testDownloadCommandAbortsClosedBeforeComplete);
  CPPUNIT_TEST(testSkipBodyRedirectsAfterEndStream);
  CPPUNIT_TEST(testSkipBodySchedulesAfterBodyProgress);
  CPPUNIT_TEST(testSkipHeadRedirectsWithContentLength);
  CPPUNIT_TEST(testSkipBodyAborts404AfterEndStream);
  CPPUNIT_TEST(testSkipBodyAbortsBodyLongerThanContentLength);
  CPPUNIT_TEST(testSkipBodyRedirectsWhenBodyShorterThanContentLength);
  CPPUNIT_TEST(testSkipBodyAborts404WhenBodyShorterThanContentLength);
  CPPUNIT_TEST(testSkipCoalesced421RetriesAfterEndStream);
  CPPUNIT_TEST(testSkipBodyAbortsOnStreamError);
  CPPUNIT_TEST(testTransportFailureThrows);
  CPPUNIT_TEST(testStreamClosedBeforeHeadersThrows);
  CPPUNIT_TEST_SUITE_END();

public:
  void testResponseCommandStartsActive();
  void testRefreshZeroRunsSkippedInactiveCommands();
  void testWaitsForHeaders();
  void testWaitsForSelectedStreamHeaders();
  void testCanSkipConnectionAccounting();
  void testZeroLengthResponseCompletes();
  void testResponseKeepsSinglePipelinedRequest();
  void testBodyDownloadCommandCreationDoesNotThrow();
  void testBodyDownloadCommandStartsActive();
  void testDownloadCommandCompletesKnownLengthBody();
  void testDownloadCommandWaitsForEndStreamAfterBody();
  void testDownloadCommandSchedulesAfterBodyProgress();
  void testDownloadCommandCompletesBodyAcrossSegments();
  void testDownloadCommandWaitsForEndStreamAcrossSegments();
  void testDownloadCommandCancelsBufferedBodyAfterRetry();
  void testDownloadCommandAbortsBodyLongerThanFile();
  void testDownloadCommandAbortsClosedBeforeComplete();
  void testSkipBodyRedirectsAfterEndStream();
  void testSkipBodySchedulesAfterBodyProgress();
  void testSkipHeadRedirectsWithContentLength();
  void testSkipBodyAborts404AfterEndStream();
  void testSkipBodyAbortsBodyLongerThanContentLength();
  void testSkipBodyRedirectsWhenBodyShorterThanContentLength();
  void testSkipBodyAborts404WhenBodyShorterThanContentLength();
  void testSkipCoalesced421RetriesAfterEndStream();
  void testSkipBodyAbortsOnStreamError();
  void testTransportFailureThrows();
  void testStreamClosedBeforeHeadersThrows();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2ResponseCommandTest);

namespace {

class TestHttp2ResponseCommand : public Http2ResponseCommand {
private:
  bool requeued_ = false;
  bool retried_ = false;
  time_t retryWait_ = -1;

public:
  TestHttp2ResponseCommand(
      cuid_t cuid, const std::shared_ptr<Request>& req,
      const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
      std::shared_ptr<Http2MultiplexExchange> exchange, int32_t streamId,
      std::unique_ptr<HttpRequest> httpRequest, DownloadEngine* e,
      const std::shared_ptr<SocketCore>& s, bool incNumConnection = true)
      : Http2ResponseCommand(cuid, req, fileEntry, requestGroup,
                             std::move(exchange), streamId,
                             std::move(httpRequest), e, s, incNumConnection)
  {
  }

  using Http2ResponseCommand::executeInternal;
  using Http2ResponseCommand::createHttpDownloadCommand;

  bool requeued() const { return requeued_; }
  bool retried() const { return retried_; }
  time_t retryWait() const { return retryWait_; }

protected:
  void requeueSelf() CXX11_OVERRIDE { requeued_ = true; }
  bool prepareForRetry(time_t wait) CXX11_OVERRIDE
  {
    retried_ = true;
    retryWait_ = wait;
    return Http2ResponseCommand::prepareForRetry(wait);
  }
};

class TestHttp2DownloadCommand : public Http2DownloadCommand {
private:
  bool requeued_ = false;

public:
  TestHttp2DownloadCommand(
      cuid_t cuid, const std::shared_ptr<Request>& req,
      const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
      std::shared_ptr<Http2MultiplexExchange> exchange, int32_t streamId,
      std::unique_ptr<HttpResponse> httpResponse, DownloadEngine* e,
      const std::shared_ptr<SocketCore>& s, bool incNumConnection = true)
      : Http2DownloadCommand(cuid, req, fileEntry, requestGroup,
                             std::move(exchange), streamId,
                             std::move(httpResponse),
                             std::unique_ptr<StreamFilter>{}, e, s,
                             incNumConnection)
  {
  }

  using Http2DownloadCommand::executeInternal;

  bool requeued() const { return requeued_; }

protected:
  void requeueSelf() CXX11_OVERRIDE { requeued_ = true; }
};

class CountingRequeueCommand : public Command {
private:
  DownloadEngine* e_;
  int* count_;
  int finishCount_;

public:
  CountingRequeueCommand(cuid_t cuid, DownloadEngine* e, int* count,
                         int finishCount)
      : Command(cuid), e_(e), count_(count), finishCount_(finishCount)
  {
  }

  bool execute() CXX11_OVERRIDE
  {
    ++*count_;
    if (*count_ >= finishCount_) {
      return true;
    }
    e_->addCommand(std::unique_ptr<Command>(this));
    return false;
  }
};

class RefreshZeroCommand : public Command {
private:
  DownloadEngine* e_;
  int* count_;

public:
  RefreshZeroCommand(cuid_t cuid, DownloadEngine* e, int* count)
      : Command(cuid), e_(e), count_(count)
  {
    setStatusActive();
  }

  bool execute() CXX11_OVERRIDE
  {
    ++*count_;
    e_->setNoWait(true);
    e_->setRefreshInterval(std::chrono::milliseconds(0));
    return true;
  }
};

std::pair<std::shared_ptr<SocketCore>, std::shared_ptr<SocketCore>>
createSocketPair()
{
  SocketCore server;
  server.bind(0);
  server.beginListen();
  server.setBlockingMode();

  auto endpoint = server.getAddrInfo();
  auto client = std::make_shared<SocketCore>();
  client->establishConnection("localhost", endpoint.port);
  CPPUNIT_ASSERT(client->isWritable(5));

  auto inbound = server.acceptConnection();
  inbound->setBlockingMode();

  return std::pair<std::shared_ptr<SocketCore>, std::shared_ptr<SocketCore>>(
      client, inbound);
}

struct CommandFixture {
  std::shared_ptr<Option> option;
  std::shared_ptr<RequestGroup> requestGroup;
  DownloadEngine engine;
  RequestGroupMan* requestGroupMan;
  std::shared_ptr<Request> request;
  std::shared_ptr<FileEntry> fileEntry;
  AuthConfigFactory authConfigFactory;
  http2test::MemoryHttp2Transport transport;
  std::shared_ptr<Http2MultiplexExchange> exchange;
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
        exchange(std::make_shared<Http2MultiplexExchange>(transport)),
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
        1, request, fileEntry, requestGroup.get(), exchange, streamId,
        makeHttpRequest(), &engine, nullptr);
  }

  std::unique_ptr<TestHttp2ResponseCommand>
  makeCommandWithSocket(const std::shared_ptr<SocketCore>& socket)
  {
    return make_unique<TestHttp2ResponseCommand>(
        1, request, fileEntry, requestGroup.get(), exchange, streamId,
        makeHttpRequest(), &engine, socket);
  }

  std::unique_ptr<TestHttp2ResponseCommand>
  makeCommandWithoutConnectionAccounting()
  {
    return make_unique<TestHttp2ResponseCommand>(
        1, request, fileEntry, requestGroup.get(), exchange, streamId,
        makeHttpRequest(), &engine, nullptr, false);
  }

  std::unique_ptr<TestHttp2DownloadCommand> makeDownloadCommand(
      std::unique_ptr<HttpResponse> httpResponse)
  {
    return make_unique<TestHttp2DownloadCommand>(
        1, request, fileEntry, requestGroup.get(), exchange,
        streamId, std::move(httpResponse), &engine, nullptr);
  }

  std::unique_ptr<TestHttp2DownloadCommand>
  makeDownloadCommandWithoutConnectionAccounting(
      std::unique_ptr<HttpResponse> httpResponse)
  {
    return make_unique<TestHttp2DownloadCommand>(
        1, request, fileEntry, requestGroup.get(), exchange,
        streamId, std::move(httpResponse), &engine, nullptr, false);
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
    auto httpResponse = exchange->createHttpResponse(streamId);
    CPPUNIT_ASSERT(httpResponse);
    httpResponse->setCuid(1);
    httpResponse->setHttpRequest(makeHttpRequest());
    return httpResponse;
  }

  std::unique_ptr<HttpResponse> receiveResponseHeaders(Http2HeaderBlock headers)
  {
    submitResponseHeaders(std::move(headers));
    CPPUNIT_ASSERT(exchange->pump());
    auto httpResponse = exchange->createHttpResponse(streamId);
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

void Http2ResponseCommandTest::testResponseCommandStartsActive()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();

  CPPUNIT_ASSERT(command->statusMatch(Command::STATUS_ACTIVE));
}

void Http2ResponseCommandTest::testRefreshZeroRunsSkippedInactiveCommands()
{
  auto option = CommandFixture::makeOption(true, 1_m);
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  engine.setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{}, 1, option.get()));

  int warmupCount = 0;
  engine.addCommand(make_unique<CountingRequeueCommand>(1, &engine,
                                                        &warmupCount, 2));
  CPPUNIT_ASSERT_EQUAL(1, engine.run(true));
  CPPUNIT_ASSERT_EQUAL(1, warmupCount);

  int inactiveCount = 0;
  int activeCount = 0;
  engine.setNoWait(true);
  engine.addCommand(make_unique<CountingRequeueCommand>(2, &engine,
                                                        &inactiveCount, 1));
  engine.addCommand(make_unique<RefreshZeroCommand>(3, &engine, &activeCount));

  CPPUNIT_ASSERT_EQUAL(0, engine.run(true));
  CPPUNIT_ASSERT_EQUAL(2, warmupCount);
  CPPUNIT_ASSERT_EQUAL(1, activeCount);
  CPPUNIT_ASSERT_EQUAL(1, inactiveCount);
}

void Http2ResponseCommandTest::testWaitsForHeaders()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
}

void Http2ResponseCommandTest::testWaitsForSelectedStreamHeaders()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto otherStreamId =
      fixture.exchange->submitRequest(http2test::createRequestHeaders());
  CPPUNIT_ASSERT(otherStreamId != fixture.streamId);
  CPPUNIT_ASSERT(fixture.exchange->flushOutboundData());
  fixture.server.feedInboundData(fixture.transport.drainOutboundData());
  fixture.server.submitResponseHeaders(otherStreamId, createHeaders(200, 0));
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
}

void Http2ResponseCommandTest::testCanSkipConnectionAccounting()
{
  CommandFixture fixture;
  CPPUNIT_ASSERT_EQUAL(0, fixture.requestGroup->getNumConnection());

  {
    auto command = fixture.makeCommandWithoutConnectionAccounting();
    CPPUNIT_ASSERT_EQUAL(0, fixture.requestGroup->getNumConnection());
  }
  CPPUNIT_ASSERT_EQUAL(0, fixture.requestGroup->getNumConnection());

  auto httpResponse =
      fixture.receiveResponse(createHeaders(200, 4), "body");
  {
    auto command = fixture.makeDownloadCommandWithoutConnectionAccounting(
        std::move(httpResponse));
    CPPUNIT_ASSERT_EQUAL(0, fixture.requestGroup->getNumConnection());
  }
  CPPUNIT_ASSERT_EQUAL(0, fixture.requestGroup->getNumConnection());
}

void Http2ResponseCommandTest::testZeroLengthResponseCompletes()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(200, 0));

  CPPUNIT_ASSERT(command->executeInternal());
}

void Http2ResponseCommandTest::testResponseKeepsSinglePipelinedRequest()
{
  CommandFixture fixture;
  fixture.option->put(PREF_MAX_HTTP_PIPELINING, "4");
  fixture.request->setPipeliningHint(true);
  fixture.request->setMaxPipelinedRequest(4);
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(200, 0));

  CPPUNIT_ASSERT(command->executeInternal());
  CPPUNIT_ASSERT(!fixture.request->isPipeliningHint());
  CPPUNIT_ASSERT_EQUAL(1, fixture.request->getMaxPipelinedRequest());
}

void Http2ResponseCommandTest::testBodyDownloadCommandCreationDoesNotThrow()
{
  CommandFixture fixture(4, false, true);
  auto command = fixture.makeCommand();
  fixture.submitResponseHeaders(createHeaders(200, 4));

  CPPUNIT_ASSERT(command->executeInternal());
}

void Http2ResponseCommandTest::testBodyDownloadCommandStartsActive()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponseHeaders(createHeaders(200, 4));
  auto command = fixture.makeCommand();
  auto downloadCommand = command->createHttpDownloadCommand(
      std::move(httpResponse), std::unique_ptr<StreamFilter>{});

  CPPUNIT_ASSERT(downloadCommand->statusMatch(Command::STATUS_ACTIVE));
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

void Http2ResponseCommandTest::testDownloadCommandSchedulesAfterBodyProgress()
{
  CommandFixture fixture(4, false, true);
  auto httpResponse = fixture.receiveResponseHeaders(createHeaders(200, 4));
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));
  command->setStatus(Command::STATUS_INACTIVE);
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
  CPPUNIT_ASSERT(command->statusMatch(Command::STATUS_ACTIVE));
  CPPUNIT_ASSERT(!fixture.requestGroup->downloadFinished());
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

void Http2ResponseCommandTest::testDownloadCommandCancelsBufferedBodyAfterRetry()
{
  CommandFixture fixture(6, false, true, 3);
  auto busySegment =
      fixture.requestGroup->getSegmentMan()->getSegmentWithIndex(2, 1);
  CPPUNIT_ASSERT(busySegment);
  busySegment->updateWrittenLength(1);

  auto httpResponse = fixture.receiveResponseHeaders(createHeaders(200, 6));
  auto command = fixture.makeDownloadCommand(std::move(httpResponse));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "abcdef");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->execute());
  CPPUNIT_ASSERT(!fixture.exchange->hasActiveStream(fixture.streamId));
  auto outbound = fixture.transport.drainOutboundData();
  CPPUNIT_ASSERT(http2test::containsFrameType(outbound, NGHTTP2_RST_STREAM));
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

void Http2ResponseCommandTest::testSkipBodySchedulesAfterBodyProgress()
{
  CommandFixture fixture;
  auto command = fixture.makeCommand();
  auto headers = createHeaders(302, 4);
  headers.emplace_back("location", "https://origin.example/next");
  fixture.submitResponseHeaders(std::move(headers));

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());

  command->setStatus(Command::STATUS_INACTIVE);
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->statusMatch(Command::STATUS_ACTIVE));
  CPPUNIT_ASSERT_EQUAL(std::string("https://origin.example/file.bin"),
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

void Http2ResponseCommandTest::testSkipCoalesced421RetriesAfterEndStream()
{
  CommandFixture fixture;
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  fixture.request->setConnectedAddrInfo("origin.example", peer.addr,
                                        peer.port);
  fixture.request->setHttp2OriginCoalesced(true);
  auto command = fixture.makeCommandWithSocket(sockets.first);
  fixture.submitResponseHeaders(createHeaders(421, 4));
  fixture.server.submitResponseDataNoEndStream(fixture.streamId, "body");
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(!command->executeInternal());
  CPPUNIT_ASSERT(command->requeued());
  CPPUNIT_ASSERT(!command->retried());
  CPPUNIT_ASSERT(fixture.request->isHttp2OriginCoalesced());
  CPPUNIT_ASSERT(!fixture.request->http2OriginCoalescingBlocked());

  fixture.server.submitEndStream(fixture.streamId);
  fixture.transport.appendInboundData(fixture.server.drainOutboundData());

  CPPUNIT_ASSERT(command->executeInternal());
  CPPUNIT_ASSERT(command->retried());
  CPPUNIT_ASSERT_EQUAL((time_t)0, command->retryWait());
  CPPUNIT_ASSERT(!fixture.request->isHttp2OriginCoalesced());
  CPPUNIT_ASSERT(fixture.request->http2OriginCoalescingBlocked());
#  ifdef ENABLE_SSL
  CPPUNIT_ASSERT(fixture.requestGroup->http2OriginCoalescingPeerBlocked(
      "https", "origin.example", 443, "origin.example", peer.addr,
      peer.port));
#  endif // ENABLE_SSL
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
