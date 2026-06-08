#include "DownloadEngine.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "a2functional.h"

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
#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_TEST(testFindActiveHttp2ConnectionHonorsRemoteStreamLimit);
#endif // HAVE_LIBNGHTTP2
  CPPUNIT_TEST_SUITE_END();

public:
  void testPopPooledSocketWithPredicate();
  void testPopPooledSocketSkipsPredicateMismatch();
  void testPopPooledSocketByAddressListWithPredicate();
#ifdef HAVE_LIBNGHTTP2
  void testFindActiveHttp2ConnectionHonorsRemoteStreamLimit();
#endif // HAVE_LIBNGHTTP2
};

CPPUNIT_TEST_SUITE_REGISTRATION(DownloadEngineTest);

namespace {

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

#ifdef HAVE_LIBNGHTTP2
void DownloadEngineTest::testFindActiveHttp2ConnectionHonorsRemoteStreamLimit()
{
  DownloadEngine e(make_unique<SelectEventPoll>());
  auto sockets = createSocketPair();
  auto peer = sockets.first->getPeerInfo();
  auto option = std::make_shared<Option>();
  RequestGroup requestGroup(GroupId::create(), option);
  auto request = std::make_shared<Request>();
  CPPUNIT_ASSERT(request->setUri("https://example.org/file"));
  request->setConnectedAddrInfo("example.org", peer.addr, peer.port);
  request->confirmConnectedAddrInfo();

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
      &requestGroup, exchange, sockets.first);
  e.registerActiveHttp2Connection(request.get(), context);

  auto active = e.findActiveHttp2Connection(
      &requestGroup, request.get(), "example.org", peer.addr, peer.port,
      std::function<bool(const std::shared_ptr<SocketCore>&)>());

  CPPUNIT_ASSERT(!active.isActive());
}
#endif // HAVE_LIBNGHTTP2

} // namespace aria2
