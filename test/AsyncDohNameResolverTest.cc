#include "AsyncDohNameResolver.h"

#ifdef ENABLE_SSL

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "AsyncDnsServerConfig.h"
#include "DnsMessage.h"
#include "EventPoll.h"
#include "a2functional.h"
#include "util.h"

namespace aria2 {

class AsyncDohNameResolverTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AsyncDohNameResolverTest);
  CPPUNIT_TEST(testResolveAResponseWithBodyInHeaderRead);
  CPPUNIT_TEST(testResolveAResponseWithFragmentedHeaderRead);
  CPPUNIT_TEST(testResolveAAAAResponse);
  CPPUNIT_TEST(testTLSHostIsUsedForHostHeaderAndHandshake);
  CPPUNIT_TEST(testHostHeaderUsesBracketedIPv6);
  CPPUNIT_TEST(testTlsWantReadUpdatesSocketEvents);
  CPPUNIT_TEST(testTlsWantWriteUpdatesSocketEvents);
  CPPUNIT_TEST(testWriteWantWriteThenSucceeds);
  CPPUNIT_TEST(testProcessTimeoutDrainsBufferedReadData);
  CPPUNIT_TEST(testProcessTimeoutDrainsFragmentedHeader);
  CPPUNIT_TEST(testRejectEmptyServerList);
  CPPUNIT_TEST(testRejectInvalidHostname);
  CPPUNIT_TEST(testRejectNon200Response);
  CPPUNIT_TEST(testRejectChunkedResponse);
  CPPUNIT_TEST(testRejectAnyTransferEncodingResponse);
  CPPUNIT_TEST(testRejectMissingContentLength);
  CPPUNIT_TEST(testRejectBadContentLength);
  CPPUNIT_TEST(testRejectBodyLargerThanContentLength);
  CPPUNIT_TEST(testResponseIdMismatchFails);
  CPPUNIT_TEST(testRetryNextServerOnConnectError);
  CPPUNIT_TEST_SUITE_END();

public:
  void testResolveAResponseWithBodyInHeaderRead();
  void testResolveAResponseWithFragmentedHeaderRead();
  void testResolveAAAAResponse();
  void testTLSHostIsUsedForHostHeaderAndHandshake();
  void testHostHeaderUsesBracketedIPv6();
  void testTlsWantReadUpdatesSocketEvents();
  void testTlsWantWriteUpdatesSocketEvents();
  void testWriteWantWriteThenSucceeds();
  void testProcessTimeoutDrainsBufferedReadData();
  void testProcessTimeoutDrainsFragmentedHeader();
  void testRejectEmptyServerList();
  void testRejectInvalidHostname();
  void testRejectNon200Response();
  void testRejectChunkedResponse();
  void testRejectAnyTransferEncodingResponse();
  void testRejectMissingContentLength();
  void testRejectBadContentLength();
  void testRejectBodyLargerThanContentLength();
  void testResponseIdMismatchFails();
  void testRetryNextServerOnConnectError();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncDohNameResolverTest);

namespace {

void appendUint16(std::string& out, uint16_t value)
{
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

void appendUint32(std::string& out, uint32_t value)
{
  out.push_back(static_cast<char>((value >> 24) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

void appendName(std::string& out, const std::string& name)
{
  size_t pos = 0;
  while (pos < name.size()) {
    auto dot = name.find('.', pos);
    auto end = dot == std::string::npos ? name.size() : dot;
    auto labelLen = end - pos;
    out.push_back(static_cast<char>(labelLen));
    out.append(name, pos, labelLen);
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }
  out.push_back(0);
}

void appendQuestion(std::string& out, const std::string& name, uint16_t type)
{
  appendName(out, name);
  appendUint16(out, type);
  appendUint16(out, 1);
}

void appendCompressedName(std::string& out, uint16_t offset)
{
  appendUint16(out, 0xc000 | offset);
}

void appendHeader(std::string& out, uint16_t id, uint16_t flags,
                  uint16_t qdcount, uint16_t ancount)
{
  appendUint16(out, id);
  appendUint16(out, flags);
  appendUint16(out, qdcount);
  appendUint16(out, ancount);
  appendUint16(out, 0);
  appendUint16(out, 0);
}

void appendAAnswer(std::string& out, uint16_t nameOffset, uint32_t ttl,
                   unsigned char a, unsigned char b, unsigned char c,
                   unsigned char d)
{
  appendCompressedName(out, nameOffset);
  appendUint16(out, dns::TYPE_A);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, 4);
  out.push_back(static_cast<char>(a));
  out.push_back(static_cast<char>(b));
  out.push_back(static_cast<char>(c));
  out.push_back(static_cast<char>(d));
}

void appendAAAAAnswer(std::string& out, uint16_t nameOffset, uint32_t ttl,
                      const unsigned char addr[16])
{
  appendCompressedName(out, nameOffset);
  appendUint16(out, dns::TYPE_AAAA);
  appendUint16(out, 1);
  appendUint32(out, ttl);
  appendUint16(out, 16);
  out.append(reinterpret_cast<const char*>(addr), 16);
}

std::string getHeaderValue(const std::string& request,
                           const std::string& name)
{
  auto pos = request.find(name);
  if (pos == std::string::npos) {
    return std::string();
  }
  pos += name.size();
  auto eol = request.find("\r\n", pos);
  if (eol == std::string::npos) {
    return std::string();
  }
  return request.substr(pos, eol - pos);
}

std::string getDohRequestBody(const std::string& request)
{
  auto pos = request.find("\r\n\r\n");
  if (pos == std::string::npos) {
    return std::string();
  }
  return request.substr(pos + 4);
}

uint16_t getQueryId(const std::string& dohRequest)
{
  auto body = getDohRequestBody(dohRequest);
  return (static_cast<uint16_t>(
              static_cast<unsigned char>(body[0])) << 8) |
         static_cast<uint16_t>(static_cast<unsigned char>(body[1]));
}

std::string createAResponse(uint16_t id, const std::string& hostname)
{
  std::string msg;
  appendHeader(msg, id, 0x8180, 1, 1);
  appendQuestion(msg, hostname, dns::TYPE_A);
  appendAAnswer(msg, 12, 60, 198, 51, 100, 9);
  return msg;
}

std::string createAAAAResponse(uint16_t id, const std::string& hostname)
{
  static const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 2};
  std::string msg;
  appendHeader(msg, id, 0x8180, 1, 1);
  appendQuestion(msg, hostname, dns::TYPE_AAAA);
  appendAAAAAnswer(msg, 12, 60, addr);
  return msg;
}

std::string createHttpResponse(const std::string& body,
                               const std::string& extraHeaders =
                                   std::string())
{
  std::string response = "HTTP/1.1 200 OK\r\nContent-Type: "
                         "application/dns-message\r\nContent-Length: ";
  response += util::uitos(body.size());
  response += "\r\n";
  response += extraHeaders;
  response += "\r\n";
  response += body;
  return response;
}

class FakeDohTransport : public AsyncDohTransport {
public:
  explicit FakeDohTransport(sock_t fd) : fd_(fd) {}

  virtual void startConnect(const std::string& host, uint16_t port)
      CXX11_OVERRIDE
  {
    connectHost = host;
    connectPort = port;
    started = true;
  }

  virtual sock_t getSocket() const CXX11_OVERRIDE { return fd_; }

  virtual std::string getSocketError() const CXX11_OVERRIDE
  {
    return socketError;
  }

  virtual bool tlsConnect(const TLSHandshakeParams& params) CXX11_OVERRIDE
  {
    tlsParams = params;
    ++tlsConnectCalls;
    wantRead_ = false;
    wantWrite_ = false;
    if (tlsBlocksWithRead) {
      tlsBlocksWithRead = false;
      wantRead_ = true;
      return false;
    }
    if (tlsBlocksWithWrite) {
      tlsBlocksWithWrite = false;
      wantWrite_ = true;
      return false;
    }
    tlsDone = true;
    return true;
  }

  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE
  {
    wantRead_ = false;
    wantWrite_ = false;
    if (writeBlocks) {
      writeBlocks = false;
      wantWrite_ = true;
      return 0;
    }
    written.append(static_cast<const char*>(data), len);
    return static_cast<ssize_t>(len);
  }

  virtual size_t readData(void* data, size_t len) CXX11_OVERRIDE
  {
    wantRead_ = false;
    wantWrite_ = false;
    if (readOffset == readBuffer.size()) {
      wantRead_ = true;
      return 0;
    }
    auto nread = std::min(len, readBuffer.size() - readOffset);
    if (maxReadSize) {
      nread = std::min(nread, maxReadSize);
    }
    memcpy(data, readBuffer.data() + readOffset, nread);
    readOffset += nread;
    return nread;
  }

  virtual size_t getRecvBufferedLength() const CXX11_OVERRIDE
  {
    if (!recvBufferedLength) {
      return 0;
    }
    return readBuffer.size() - readOffset;
  }

  virtual bool wantRead() const CXX11_OVERRIDE { return wantRead_; }

  virtual bool wantWrite() const CXX11_OVERRIDE { return wantWrite_; }

  sock_t fd_;
  std::string connectHost;
  uint16_t connectPort = 0;
  bool started = false;
  std::string socketError;
  TLSHandshakeParams tlsParams;
  int tlsConnectCalls = 0;
  bool tlsBlocksWithRead = false;
  bool tlsBlocksWithWrite = false;
  bool tlsDone = false;
  bool writeBlocks = false;
  std::string written;
  std::string readBuffer;
  size_t readOffset = 0;
  size_t maxReadSize = 0;
  size_t recvBufferedLength = 0;
  bool wantRead_ = false;
  bool wantWrite_ = false;
};

class FakeDohTransportFactory {
public:
  std::unique_ptr<AsyncDohTransport>
  operator()(const AsyncDohServerConfig& server)
  {
    auto transport = make_unique<FakeDohTransport>(nextFd++);
    transports.push_back(transport.get());
    if (server.connectHost == "198.51.100.1") {
      transport->socketError = "connection refused";
    }
    std::unique_ptr<AsyncDohTransport> baseTransport(std::move(transport));
    return baseTransport;
  }

  sock_t nextFd = 700;
  std::vector<FakeDohTransport*> transports;
};

AsyncDohTransportFactory makeTransportFactory(FakeDohTransportFactory& factory)
{
  return [&factory](const AsyncDohServerConfig& server) {
    return factory(server);
  };
}

void driveOnce(AsyncDohNameResolver& resolver, FakeDohTransport* transport)
{
  const auto& socks = resolver.getsock();
  CPPUNIT_ASSERT_EQUAL((size_t)1, socks.size());
  if (socks[0].events & EventPoll::EVENT_READ) {
    resolver.process(socks[0].fd, AsyncResolver::badSocket());
  }
  else {
    resolver.process(AsyncResolver::badSocket(), socks[0].fd);
  }
  (void)transport;
}

void driveUntilWriteRequestDone(AsyncDohNameResolver& resolver,
                                FakeDohTransportFactory& factory)
{
  for (size_t i = 0; i < 8; ++i) {
    auto transport = factory.transports.back();
    driveOnce(resolver, transport);
    if (!transport->written.empty()) {
      return;
    }
  }
  CPPUNIT_FAIL("DoH request was not written");
}

void driveUntilDone(AsyncDohNameResolver& resolver,
                    FakeDohTransportFactory& factory)
{
  for (size_t i = 0; i < 256; ++i) {
    if (resolver.getStatus() == AsyncResolver::STATUS_SUCCESS ||
        resolver.getStatus() == AsyncResolver::STATUS_ERROR) {
      return;
    }
    driveOnce(resolver, factory.transports.back());
  }
  CPPUNIT_FAIL("DoH resolver did not finish");
}

} // namespace

void AsyncDohNameResolverTest::testResolveAResponseWithBodyInHeaderRead()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_CONNECTING,
                       resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);

  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  CPPUNIT_ASSERT(transport->started);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT(transport->tlsParams.alpnProtocols.empty());
  CPPUNIT_ASSERT(
      util::startsWith(transport->written, "POST /dns-query HTTP/1.1\r\n"));
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"),
                       getHeaderValue(transport->written, "Host: "));
  CPPUNIT_ASSERT_EQUAL(std::string("application/dns-message"),
                       getHeaderValue(transport->written, "Content-Type: "));

  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createHttpResponse(response);

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_DONE, resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
  CPPUNIT_ASSERT(resolver.getsock().empty());
}

void AsyncDohNameResolverTest::testResolveAResponseWithFragmentedHeaderRead()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createHttpResponse(response);
  transport->maxReadSize = 1;

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
}

void AsyncDohNameResolverTest::testResolveAAAAResponse()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET6, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  transport->readBuffer = createHttpResponse(createAAAAResponse(
      getQueryId(transport->written), "www.example.com"));

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::2"),
                       resolver.getResolvedAddresses()[0]);
}

void AsyncDohNameResolverTest::testTLSHostIsUsedForHostHeaderAndHandshake()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 8443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org:8443"),
                       getHeaderValue(transport->written, "Host: "));
}

void AsyncDohNameResolverTest::testHostHeaderUsesBracketedIPv6()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET,
      {{"2606:4700:4700::1111", 8443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("[2606:4700:4700::1111]:8443"),
                       getHeaderValue(transport->written, "Host: "));
}

void AsyncDohNameResolverTest::testTlsWantReadUpdatesSocketEvents()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();
  transport->tlsBlocksWithRead = true;

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_TLS_HANDSHAKING,
                       resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_READ,
                       resolver.getsock()[0].events);
}

void AsyncDohNameResolverTest::testTlsWantWriteUpdatesSocketEvents()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();
  transport->tlsBlocksWithWrite = true;

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_TLS_HANDSHAKING,
                       resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);
}

void AsyncDohNameResolverTest::testWriteWantWriteThenSucceeds()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);
  transport->writeBlocks = true;
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_WRITING_REQUEST,
                       resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);
  CPPUNIT_ASSERT(transport->written.empty());

  driveOnce(resolver, transport);
  CPPUNIT_ASSERT(!transport->written.empty());
}

void AsyncDohNameResolverTest::testProcessTimeoutDrainsBufferedReadData()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createHttpResponse(response);
  transport->recvBufferedLength = 1;

  resolver.processTimeout();

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_DONE, resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
  CPPUNIT_ASSERT(resolver.getsock().empty());
}

void AsyncDohNameResolverTest::testProcessTimeoutDrainsFragmentedHeader()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createHttpResponse(response);
  transport->maxReadSize = 1;
  transport->recvBufferedLength = 1;

  resolver.processTimeout();

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
}

void AsyncDohNameResolverTest::testRejectEmptyServerList()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(AF_INET, {}, makeTransportFactory(factory));

  resolver.resolve("www.example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_FAILED,
                       resolver.getDohState());
  CPPUNIT_ASSERT(!resolver.getError().empty());
  CPPUNIT_ASSERT(factory.transports.empty());
}

void AsyncDohNameResolverTest::testRejectInvalidHostname()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www..example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_FAILED,
                       resolver.getDohState());
  CPPUNIT_ASSERT(!resolver.getError().empty());
  CPPUNIT_ASSERT(factory.transports.empty());
}

void AsyncDohNameResolverTest::testRejectNon200Response()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 404 Not Found\r\nContent-Length: 1\r\n\r\nx";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRejectChunkedResponse()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRejectAnyTransferEncodingResponse()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRejectMissingContentLength()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 200 OK\r\nContent-Type: application/dns-message\r\n\r\n";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRejectBadContentLength()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 200 OK\r\nContent-Length: nope\r\n\r\n";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRejectBodyLargerThanContentLength()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  factory.transports.back()->readBuffer =
      "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nxx";

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testResponseIdMismatchFails()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response =
      createAResponse(getQueryId(transport->written) + 1, "www.example.com");
  transport->readBuffer = createHttpResponse(response);

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDohNameResolverTest::testRetryNextServerOnConnectError()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(AF_INET,
                                {{"198.51.100.1", 443, "", "/dns-query"},
                                 {"1.1.1.1", 443, "", "/dns-query"}},
                                makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  CPPUNIT_ASSERT_EQUAL((size_t)1, factory.transports.size());

  driveOnce(resolver, factory.transports.back());
  CPPUNIT_ASSERT_EQUAL((size_t)2, factory.transports.size());
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"),
                       factory.transports.back()->connectHost);

  driveUntilWriteRequestDone(resolver, factory);
  auto transport = factory.transports.back();
  transport->readBuffer = createHttpResponse(createAResponse(
      getQueryId(transport->written), "www.example.com"));

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
}

} // namespace aria2

#endif // ENABLE_SSL
