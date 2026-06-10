#include "DownloadEngine.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "File.h"
#include "LogFactory.h"
#include "Logger.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "TestUtil.h"
#include "a2functional.h"
#include "prefs.h"

#ifdef HAVE_LIBNGHTTP2
#  include "GroupId.h"
#  include "Http2ConnectionContext.h"
#  include "Http2MultiplexExchange.h"
#  include "Http2TestUtil.h"
#  include "Option.h"
#  include "Request.h"
#  include "RequestGroup.h"
#endif // HAVE_LIBNGHTTP2

namespace aria2 {

class DownloadEngineTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(DownloadEngineTest);
  CPPUNIT_TEST(testPopPooledSocketWithPredicate);
  CPPUNIT_TEST(testPopPooledSocketSkipsPredicateMismatch);
  CPPUNIT_TEST(testPopPooledSocketByAddressListWithPredicate);
  CPPUNIT_TEST(testMarkBadIPAddressLogsAddressFamily);
#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_TEST(testFindActiveHttp2ConnectionHonorsRemoteStreamLimit);
  CPPUNIT_TEST(testFindActiveHttp2ConnectionReusesUntilRemoteStreamLimit);
  CPPUNIT_TEST(testFindActiveHttp2ConnectionCoalescesOrigin);
  CPPUNIT_TEST(testFindActiveHttp2ConnectionSkipsProxiedCoalescing);
  CPPUNIT_TEST(testHttp2ConnectionContextKeepsRequestGroupAlive);
  CPPUNIT_TEST(testPopIdleHttp2Connection);
  CPPUNIT_TEST(testPopIdleHttp2ConnectionCoalescesOrigin);
  CPPUNIT_TEST(testPopIdleHttp2ConnectionSkipsProxiedCoalescing);
  CPPUNIT_TEST(testPoolIdleHttp2ConnectionSkipsActiveExchange);
#endif // HAVE_LIBNGHTTP2
  CPPUNIT_TEST_SUITE_END();

public:
  void testPopPooledSocketWithPredicate();
  void testPopPooledSocketSkipsPredicateMismatch();
  void testPopPooledSocketByAddressListWithPredicate();
  void testMarkBadIPAddressLogsAddressFamily();
#ifdef HAVE_LIBNGHTTP2
  void testFindActiveHttp2ConnectionHonorsRemoteStreamLimit();
  void testFindActiveHttp2ConnectionReusesUntilRemoteStreamLimit();
  void testFindActiveHttp2ConnectionCoalescesOrigin();
  void testFindActiveHttp2ConnectionSkipsProxiedCoalescing();
  void testHttp2ConnectionContextKeepsRequestGroupAlive();
  void testPopIdleHttp2Connection();
  void testPopIdleHttp2ConnectionCoalescesOrigin();
  void testPopIdleHttp2ConnectionSkipsProxiedCoalescing();
  void testPoolIdleHttp2ConnectionSkipsActiveExchange();
#endif // HAVE_LIBNGHTTP2
};

CPPUNIT_TEST_SUITE_REGISTRATION(DownloadEngineTest);

namespace {

class ScopedNetworkLog {
public:
  explicit ScopedNetworkLog(std::string path) : path_(std::move(path))
  {
    File(path_).remove();
    LogFactory::setLogFile(path_);
    LogFactory::setLogLevel(V_NETWORK);
    LogFactory::reconfigure();
  }

  ~ScopedNetworkLog()
  {
    LogFactory::setLogFile("");
    LogFactory::setLogLevel(Logger::A2_DEBUG);
    LogFactory::reconfigure();
  }

  std::string closeAndRead() const
  {
    LogFactory::setLogFile("");
    LogFactory::reconfigure();
    return readFile(path_);
  }

private:
  std::string path_;
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

} // namespace

void DownloadEngineTest::testPopPooledSocketWithPredicate()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();

  e.poolSocket(peer.addr, peer.port, "", 0, sockets.first);

  auto pooled =
      e.popPooledSocket(peer.addr, peer.port, "", 0,
                        [](const std::shared_ptr<SocketCore>&) {
                          return false;
                        });
  CPPUNIT_ASSERT(!pooled);

  pooled = e.popPooledSocket(peer.addr, peer.port, "", 0,
                             [](const std::shared_ptr<SocketCore>&) {
                               return true;
                             });
  CPPUNIT_ASSERT(pooled == sockets.first);
}

void DownloadEngineTest::testPopPooledSocketSkipsPredicateMismatch()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets1 = createSocketPair();
  auto sockets2 = createSocketPair();
  auto peer = sockets1.first->getPeerInfo();

  e.poolSocket(peer.addr, peer.port, "", 0, sockets1.first);
  e.poolSocket(peer.addr, peer.port, "", 0, sockets2.first);

  bool sawMismatch = false;
  auto pooled = e.popPooledSocket(
      peer.addr, peer.port, "", 0,
      [&sockets1, &sockets2,
       &sawMismatch](const std::shared_ptr<SocketCore>& socket) {
        if (socket == sockets1.first) {
          sawMismatch = true;
          return false;
        }
        return socket == sockets2.first;
      });
  CPPUNIT_ASSERT(sawMismatch);
  CPPUNIT_ASSERT(pooled == sockets2.first);

  pooled = e.popPooledSocket(
      peer.addr, peer.port, "", 0,
      [&sockets1](const std::shared_ptr<SocketCore>& socket) {
        return socket == sockets1.first;
      });
  CPPUNIT_ASSERT(pooled == sockets1.first);
}

void DownloadEngineTest::testPopPooledSocketByAddressListWithPredicate()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();

  e.poolSocket(peer.addr, peer.port, "", 0, sockets.first);

  std::vector<std::string> addrs;
  addrs.push_back(peer.addr);

  auto pooled =
      e.popPooledSocket(addrs, peer.port,
                        [](const std::shared_ptr<SocketCore>&) {
                          return false;
                        });
  CPPUNIT_ASSERT(!pooled);

  pooled = e.popPooledSocket(addrs, peer.port,
                             [](const std::shared_ptr<SocketCore>&) {
                               return true;
                             });
  CPPUNIT_ASSERT(pooled == sockets.first);
}

void DownloadEngineTest::testMarkBadIPAddressLogsAddressFamily()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_DownloadEngineTest_testMarkBadIPAddressLogsAddressFamily.log";
  ScopedNetworkLog log(logPath);
  DownloadEngine e(make_unique<SelectEventPoll>());

  e.cacheIPAddress("example.org", "192.0.2.1", 443);
  e.cacheIPAddress("example.org", "2001:db8::1", 443);
  e.markBadIPAddress(42, "example.org", "192.0.2.1", 443);
  e.markBadIPAddress("example.org", "2001:db8::1", 443);

  CPPUNIT_ASSERT_EQUAL(std::string(), e.findCachedIPAddress("example.org", 443));

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: CUID#42 - marking bad address "
                           "host=example.org port=443 ip=192.0.2.1 "
                           "family=IPv4") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: marking bad address host=example.org "
                           "port=443 ip=2001:db8::1 family=IPv6") !=
                 std::string::npos);
}

#ifdef HAVE_LIBNGHTTP2
std::shared_ptr<Request> createHttp2Request(
    const std::string& addr, uint16_t port,
    const std::string& uri = "https://example.org/file")
{
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri(uri));
  request->setConnectedAddrInfo(request->getHost(), addr, port);
  request->confirmConnectedAddrInfo();
  request->supportsPersistentConnection(true);
  return request;
}

std::shared_ptr<Http2MultiplexExchange> createHttp2Exchange()
{
  return std::make_shared<Http2MultiplexExchange>(
      make_unique<http2test::MemoryHttp2Transport>());
}

void DownloadEngineTest::testFindActiveHttp2ConnectionHonorsRemoteStreamLimit()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);

  auto transport = make_unique<http2test::MemoryHttp2Transport>();
  auto rawTransport = transport.get();
  auto exchange =
      std::make_shared<Http2MultiplexExchange>(std::move(transport));
  http2test::FakeHttp2ServerSession server;
  server.submitMaxConcurrentStreams(1);
  rawTransport->appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange->readInboundData());
  exchange->submitRequest(http2test::createRequestHeaders());

  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);
  e.registerActiveHttp2Connection(request.get(), context);

  auto active = e.findActiveHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());

  CPPUNIT_ASSERT(!active.isActive());
}

void DownloadEngineTest::testFindActiveHttp2ConnectionReusesUntilRemoteStreamLimit()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);

  auto transport = make_unique<http2test::MemoryHttp2Transport>();
  auto rawTransport = transport.get();
  auto exchange =
      std::make_shared<Http2MultiplexExchange>(std::move(transport));
  http2test::FakeHttp2ServerSession server;
  server.submitMaxConcurrentStreams(2);
  rawTransport->appendInboundData(server.drainOutboundData());
  CPPUNIT_ASSERT(exchange->readInboundData());
  exchange->submitRequest(http2test::createRequestHeaders());

  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);
  e.registerActiveHttp2Connection(request.get(), context);

  auto active = e.findActiveHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());

  CPPUNIT_ASSERT(active.isActive());
  CPPUNIT_ASSERT(active.context == context);
  CPPUNIT_ASSERT(active.exchange == exchange);
  CPPUNIT_ASSERT(active.socket == sockets.first);

  active.exchange->submitRequest(http2test::createRequestHeaders());

  active = e.findActiveHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());

  CPPUNIT_ASSERT(!active.isActive());
}

void DownloadEngineTest::testFindActiveHttp2ConnectionCoalescesOrigin()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto coalescedRequest =
      createHttp2Request(peer.addr, peer.port, "https://cdn.example/file");
  auto exchange = createHttp2Exchange();
  exchange->submitRequest(http2test::createRequestHeaders());
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);
  e.registerActiveHttp2Connection(request.get(), context);

  auto active = e.findActiveHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      [](const std::shared_ptr<SocketCore>&) { return false; },
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!active.isActive());

  active = e.findActiveHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", "203.0.113.1",
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!active.isActive());

  active = e.findActiveHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return false; });
  CPPUNIT_ASSERT(!active.isActive());

  active = e.findActiveHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });

  CPPUNIT_ASSERT(active.isActive());
  CPPUNIT_ASSERT(active.originCoalesced);
  CPPUNIT_ASSERT(active.context == context);
  CPPUNIT_ASSERT(active.exchange == exchange);
  CPPUNIT_ASSERT(active.socket == sockets.first);
}

void DownloadEngineTest::testFindActiveHttp2ConnectionSkipsProxiedCoalescing()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto coalescedRequest =
      createHttp2Request(peer.addr, peer.port, "https://cdn.example/file");
  auto exchange = createHttp2Exchange();
  exchange->submitRequest(http2test::createRequestHeaders());
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first, true);
  e.registerActiveHttp2Connection(request.get(), context);

  auto active = e.findActiveHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!active.isActive());

  active = e.findActiveHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      [](const std::shared_ptr<SocketCore>&) { return true; },
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(active.isActive());
  CPPUNIT_ASSERT(!active.originCoalesced);
}

void DownloadEngineTest::testHttp2ConnectionContextKeepsRequestGroupAlive()
{
  auto sockets = createSocketPair();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  std::weak_ptr<RequestGroup> weakRequestGroup = requestGroup;
  auto exchange = createHttp2Exchange();
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);

  requestGroup.reset();
  CPPUNIT_ASSERT(!weakRequestGroup.expired());

  context.reset();
  CPPUNIT_ASSERT(weakRequestGroup.expired());
}

void DownloadEngineTest::testPopIdleHttp2Connection()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto exchange = createHttp2Exchange();
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);

  e.poolIdleHttp2Connection(request.get(), context);

  auto idle = e.popIdleHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());

  CPPUNIT_ASSERT(idle.isActive());
  CPPUNIT_ASSERT(idle.context == context);
  CPPUNIT_ASSERT(idle.exchange == exchange);
  CPPUNIT_ASSERT(idle.socket == sockets.first);

  idle = e.popIdleHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());
  CPPUNIT_ASSERT(!idle.isActive());
}

void DownloadEngineTest::testPopIdleHttp2ConnectionCoalescesOrigin()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto coalescedRequest =
      createHttp2Request(peer.addr, peer.port, "https://cdn.example/file");
  auto exchange = createHttp2Exchange();
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);

  e.poolIdleHttp2Connection(request.get(), context);

  auto idle = e.popIdleHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      [](const std::shared_ptr<SocketCore>&) { return false; },
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!idle.isActive());

  idle = e.popIdleHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", "203.0.113.1",
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!idle.isActive());

  idle = e.popIdleHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return false; });
  CPPUNIT_ASSERT(!idle.isActive());

  idle = e.popIdleHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });

  CPPUNIT_ASSERT(idle.isActive());
  CPPUNIT_ASSERT(idle.originCoalesced);
  CPPUNIT_ASSERT(idle.context == context);
  CPPUNIT_ASSERT(idle.exchange == exchange);
  CPPUNIT_ASSERT(idle.socket == sockets.first);
}

void DownloadEngineTest::testPopIdleHttp2ConnectionSkipsProxiedCoalescing()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto coalescedRequest =
      createHttp2Request(peer.addr, peer.port, "https://cdn.example/file");
  auto exchange = createHttp2Exchange();
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first, true);

  e.poolIdleHttp2Connection(request.get(), context);

  auto idle = e.popIdleHttp2Connection(
      requestGroup.get(), coalescedRequest.get(), "cdn.example", peer.addr,
      peer.port, std::function<bool(const std::shared_ptr<SocketCore>&)>(),
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(!idle.isActive());

  idle = e.popIdleHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      [](const std::shared_ptr<SocketCore>&) { return true; },
      [](const std::shared_ptr<SocketCore>&) { return true; });
  CPPUNIT_ASSERT(idle.isActive());
  CPPUNIT_ASSERT(!idle.originCoalesced);
}

void DownloadEngineTest::testPoolIdleHttp2ConnectionSkipsActiveExchange()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  auto requestGroup = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto request = createHttp2Request(peer.addr, peer.port);
  auto exchange = createHttp2Exchange();
  exchange->submitRequest(http2test::createRequestHeaders());
  auto context = std::make_shared<Http2ConnectionContext>(
      requestGroup, exchange, sockets.first);

  e.poolIdleHttp2Connection(request.get(), context);

  auto idle = e.popIdleHttp2Connection(
      requestGroup.get(), request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());
  CPPUNIT_ASSERT(!idle.isActive());
}
#endif // HAVE_LIBNGHTTP2

} // namespace aria2
