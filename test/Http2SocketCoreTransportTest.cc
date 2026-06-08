#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2SocketCoreTransport.h"

#  include <memory>
#  include <string>
#  include <utility>

#  include <cppunit/extensions/HelperMacros.h>

#  include "SocketCore.h"
#  include "a2functional.h"

namespace aria2 {

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
  client->setBlockingMode();
  inbound->setBlockingMode();

  return std::make_pair(client, inbound);
}
} // namespace

class Http2SocketCoreTransportTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2SocketCoreTransportTest);
  CPPUNIT_TEST(testWriteDataUsesSocketCore);
  CPPUNIT_TEST(testReadDataUsesSocketCore);
  CPPUNIT_TEST(testWantFlagsUseSocketCore);
  CPPUNIT_TEST_SUITE_END();

public:
  void testWriteDataUsesSocketCore();
  void testReadDataUsesSocketCore();
  void testWantFlagsUseSocketCore();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2SocketCoreTransportTest);

void Http2SocketCoreTransportTest::testWriteDataUsesSocketCore()
{
  auto sockets = createSocketPair();
  Http2SocketCoreTransport transport(sockets.first);
  std::string request = "client-bytes";
  char buf[64];
  size_t nread = sizeof(buf);

  auto nwrite = transport.writeData(request.data(), request.size());
  sockets.second->readData(buf, nread);

  CPPUNIT_ASSERT_EQUAL(static_cast<ssize_t>(request.size()), nwrite);
  CPPUNIT_ASSERT_EQUAL(request.size(), nread);
  CPPUNIT_ASSERT_EQUAL(request, std::string(buf, nread));
}

void Http2SocketCoreTransportTest::testReadDataUsesSocketCore()
{
  auto sockets = createSocketPair();
  Http2SocketCoreTransport transport(sockets.first);
  std::string response = "server-bytes";
  char buf[64];

  sockets.second->writeData(response.data(), response.size());
  auto nread = transport.readData(buf, sizeof(buf));

  CPPUNIT_ASSERT_EQUAL(static_cast<ssize_t>(response.size()), nread);
  CPPUNIT_ASSERT_EQUAL(response, std::string(buf, static_cast<size_t>(nread)));
}

void Http2SocketCoreTransportTest::testWantFlagsUseSocketCore()
{
  auto sockets = createSocketPair();
  Http2SocketCoreTransport transport(sockets.first);
  char buf[1];

  sockets.first->setNonBlockingMode();
  auto nread = transport.readData(buf, sizeof(buf));

  CPPUNIT_ASSERT_EQUAL((ssize_t)0, nread);
  CPPUNIT_ASSERT(transport.wantRead() || transport.wantWrite());
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
