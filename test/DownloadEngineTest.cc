#include "DownloadEngine.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "a2functional.h"

namespace aria2 {

class DownloadEngineTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(DownloadEngineTest);
  CPPUNIT_TEST(testPopPooledSocketWithPredicate);
  CPPUNIT_TEST(testPopPooledSocketSkipsPredicateMismatch);
  CPPUNIT_TEST(testPopPooledSocketByAddressListWithPredicate);
  CPPUNIT_TEST_SUITE_END();

public:
  void testPopPooledSocketWithPredicate();
  void testPopPooledSocketSkipsPredicateMismatch();
  void testPopPooledSocketByAddressListWithPredicate();
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

} // namespace aria2
