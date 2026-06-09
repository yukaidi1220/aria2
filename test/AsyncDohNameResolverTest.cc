#include "AsyncDohNameResolver.h"

#ifdef ENABLE_SSL

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "AsyncDnsServerConfig.h"
#include "DnsMessage.h"
#include "EventPoll.h"
#ifdef HAVE_LIBNGHTTP2
#  include "Http2TestUtil.h"
#  include "HttpProtocol.h"
#  include <nghttp2/nghttp2.h>
#endif // HAVE_LIBNGHTTP2
#include "a2functional.h"
#include "util.h"

namespace aria2 {

class AsyncDohNameResolverTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AsyncDohNameResolverTest);
  CPPUNIT_TEST(testResolveAResponseWithBodyInHeaderRead);
  CPPUNIT_TEST(testResolveAResponseWithFragmentedHeaderRead);
  CPPUNIT_TEST(testResolveAAAAResponse);
  CPPUNIT_TEST(testRequestPathPreservesQuery);
  CPPUNIT_TEST(testTLSHostIsUsedForHostHeaderAndHandshake);
  CPPUNIT_TEST(testTLSHostDefaultPortHostHeaderOmitsPort);
  CPPUNIT_TEST(testHostHeaderUsesBracketedIPv6);
  CPPUNIT_TEST(testHostHeaderUsesBracketedIPv6DefaultPort);
#ifdef HAVE_LIBNGHTTP2
  CPPUNIT_TEST(testHttp2EnabledAdvertisesAlpnAndFallsBackToHttp1);
  CPPUNIT_TEST(testResolveAResponseOverHttp2);
  CPPUNIT_TEST(testRetryNextServerOnHttp2RstBeforeHeaders);
#endif // HAVE_LIBNGHTTP2
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
  CPPUNIT_TEST(testDomainServerUsesBootstrapAddress);
  CPPUNIT_TEST(testDomainServerUsesAsyncBootstrapAddress);
  CPPUNIT_TEST(testDomainServerRetriesNextBootstrapAddress);
  CPPUNIT_TEST(testDomainServerRetriesNextServerOnBootstrapError);
  CPPUNIT_TEST_SUITE_END();

public:
  void testResolveAResponseWithBodyInHeaderRead();
  void testResolveAResponseWithFragmentedHeaderRead();
  void testResolveAAAAResponse();
  void testRequestPathPreservesQuery();
  void testTLSHostIsUsedForHostHeaderAndHandshake();
  void testTLSHostDefaultPortHostHeaderOmitsPort();
  void testHostHeaderUsesBracketedIPv6();
  void testHostHeaderUsesBracketedIPv6DefaultPort();
#ifdef HAVE_LIBNGHTTP2
  void testHttp2EnabledAdvertisesAlpnAndFallsBackToHttp1();
  void testResolveAResponseOverHttp2();
  void testRetryNextServerOnHttp2RstBeforeHeaders();
#endif // HAVE_LIBNGHTTP2
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
  void testDomainServerUsesBootstrapAddress();
  void testDomainServerUsesAsyncBootstrapAddress();
  void testDomainServerRetriesNextBootstrapAddress();
  void testDomainServerRetriesNextServerOnBootstrapError();
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

uint16_t getDnsMessageId(const std::string& body)
{
  return (static_cast<uint16_t>(
              static_cast<unsigned char>(body[0])) << 8) |
         static_cast<uint16_t>(static_cast<unsigned char>(body[1]));
}

uint16_t getQueryId(const std::string& dohRequest)
{
  return getDnsMessageId(getDohRequestBody(dohRequest));
}

#ifdef HAVE_LIBNGHTTP2
bool findFramePayload(const std::string& data, unsigned char frameType,
                      int32_t streamId, std::string& payload,
                      uint8_t& flags)
{
  size_t offset = 0;
  if (data.size() >= 24 &&
      data.substr(0, 24) == "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") {
    offset = 24;
  }

  while (offset + 9 <= data.size()) {
    auto length = (static_cast<unsigned char>(data[offset]) << 16) |
                  (static_cast<unsigned char>(data[offset + 1]) << 8) |
                  static_cast<unsigned char>(data[offset + 2]);
    auto type = static_cast<unsigned char>(data[offset + 3]);
    flags = static_cast<uint8_t>(data[offset + 4]);
    auto sid = ((static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 5])) << 24) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 6])) << 16) |
                (static_cast<uint32_t>(
                     static_cast<unsigned char>(data[offset + 7])) << 8) |
                static_cast<uint32_t>(
                    static_cast<unsigned char>(data[offset + 8]))) &
               0x7fffffffu;
    if (offset + 9 + length > data.size()) {
      return false;
    }
    if (type == frameType && static_cast<int32_t>(sid) == streamId) {
      payload.assign(data, offset + 9, length);
      return true;
    }
    offset += 9 + length;
  }
  return false;
}
#endif // HAVE_LIBNGHTTP2

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
    if (connectHost == "198.51.100.1") {
      socketError = "connection refused";
    }
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

  virtual std::string getSelectedAlpnProtocol() const CXX11_OVERRIDE
  {
    return selectedAlpnProtocol;
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
  std::string selectedAlpnProtocol;
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
    (void)server;
    std::unique_ptr<AsyncDohTransport> baseTransport(std::move(transport));
    return baseTransport;
  }

  sock_t nextFd = 700;
  std::vector<FakeDohTransport*> transports;
};

class FakeBootstrapResolver : public AsyncResolver {
public:
  FakeBootstrapResolver(std::vector<std::string> addrs,
                        STATUS status = STATUS_SUCCESS,
                        std::string error = std::string(),
                        std::vector<std::string>* hostnames = nullptr)
      : addrs_(std::move(addrs)),
        error_(std::move(error)),
        status_(status),
        hostnames_(hostnames),
        fd_(800)
  {
    if (status_ == STATUS_QUERYING) {
      socks_.push_back(AsyncResolverSocketEntry{fd_, EventPoll::EVENT_READ});
    }
  }

  virtual void resolve(const std::string& name) CXX11_OVERRIDE
  {
    hostname_ = name;
    if (hostnames_) {
      hostnames_->push_back(name);
    }
  }

  virtual const std::vector<std::string>& getResolvedAddresses() const
      CXX11_OVERRIDE
  {
    return addrs_;
  }

  virtual const std::string& getError() const CXX11_OVERRIDE { return error_; }

  virtual STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  virtual bool usable() const CXX11_OVERRIDE
  {
    return status_ == STATUS_QUERYING;
  }

  virtual int getFamily() const CXX11_OVERRIDE { return AF_UNSPEC; }

  virtual const std::vector<AsyncResolverSocketEntry>& getsock() const
      CXX11_OVERRIDE
  {
    return socks_;
  }

  virtual void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE
  {
    (void)writefd;
    if (readfd == fd_ || readfd == badSocket()) {
      status_ = nextStatus_;
      socks_.clear();
    }
  }

  virtual const std::string& getHostname() const CXX11_OVERRIDE
  {
    return hostname_;
  }

  STATUS nextStatus_ = STATUS_SUCCESS;

private:
  std::vector<std::string> addrs_;
  std::string error_;
  STATUS status_;
  std::vector<std::string>* hostnames_;
  std::string hostname_;
  std::vector<AsyncResolverSocketEntry> socks_;
  sock_t fd_;
};

class FakeBootstrapResolverFactory {
public:
  std::unique_ptr<AsyncResolver> operator()(int family)
  {
    CPPUNIT_ASSERT(index < results.size());
    CPPUNIT_ASSERT(index < statuses.size());
    CPPUNIT_ASSERT(index < errors.size());
    families.push_back(family);
    auto resolver = make_unique<FakeBootstrapResolver>(
        results[index], statuses[index], errors[index], &hostnames);
    if (index + 1 < results.size()) {
      ++index;
    }
    std::unique_ptr<AsyncResolver> baseResolver(std::move(resolver));
    return baseResolver;
  }

  size_t index = 0;
  std::vector<std::vector<std::string>> results;
  std::vector<AsyncResolver::STATUS> statuses;
  std::vector<std::string> errors;
  std::vector<std::string> hostnames;
  std::vector<int> families;
};

AsyncDohTransportFactory makeTransportFactory(FakeDohTransportFactory& factory)
{
  return [&factory](const AsyncDohServerConfig& server) {
    return factory(server);
  };
}

AsyncDohBootstrapResolverFactory
makeBootstrapResolverFactory(FakeBootstrapResolverFactory& factory)
{
  return [&factory](int family) { return factory(family); };
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

void AsyncDohNameResolverTest::testRequestPathPreservesQuery()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET,
      {{"1.1.1.1", 443, "", "/dns-query?ct=application/dns-message"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT(util::startsWith(
      transport->written,
      "POST /dns-query?ct=application/dns-message HTTP/1.1\r\n"));
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

void AsyncDohNameResolverTest::testTLSHostDefaultPortHostHeaderOmitsPort()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
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

void AsyncDohNameResolverTest::testHostHeaderUsesBracketedIPv6DefaultPort()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET,
      {{"2606:4700:4700::1111", 443, "", "/dns-query"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT_EQUAL(std::string("[2606:4700:4700::1111]"),
                       getHeaderValue(transport->written, "Host: "));
}

#ifdef HAVE_LIBNGHTTP2
void AsyncDohNameResolverTest::testHttp2EnabledAdvertisesAlpnAndFallsBackToHttp1()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory), true);

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL((size_t)2, transport->tlsParams.alpnProtocols.size());
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_H2),
                       transport->tlsParams.alpnProtocols[0]);
  CPPUNIT_ASSERT_EQUAL(std::string(HTTP_ALPN_HTTP11),
                       transport->tlsParams.alpnProtocols[1]);
  CPPUNIT_ASSERT(
      util::startsWith(transport->written, "POST /dns-query HTTP/1.1\r\n"));
}

void AsyncDohNameResolverTest::testResolveAResponseOverHttp2()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(
      AF_INET, {{"1.1.1.1", 443, "", "/dns-query"}},
      makeTransportFactory(factory), true);

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();
  transport->selectedAlpnProtocol = HTTP_ALPN_H2;
  driveUntilWriteRequestDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL((size_t)2, transport->tlsParams.alpnProtocols.size());
  std::string dnsQuery;
  uint8_t flags = 0;
  CPPUNIT_ASSERT(findFramePayload(transport->written, NGHTTP2_DATA, 1,
                                  dnsQuery, flags));
  CPPUNIT_ASSERT(flags & NGHTTP2_FLAG_END_STREAM);

  http2test::FakeHttp2ServerSession server;
  server.feedInboundData(transport->written);
  auto response = createAResponse(getDnsMessageId(dnsQuery),
                                  "www.example.com");
  auto headers = http2test::createResponseHeaders();
  headers.emplace_back("content-length", util::uitos(response.size()));
  server.submitResponse(1, headers, response);
  transport->readBuffer = server.drainOutboundData();

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_DONE, resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
}

void AsyncDohNameResolverTest::testRetryNextServerOnHttp2RstBeforeHeaders()
{
  FakeDohTransportFactory factory;
  AsyncDohNameResolver resolver(AF_INET,
                                {{"1.1.1.1", 443, "", "/dns-query"},
                                 {"8.8.8.8", 443, "", "/dns-query"}},
                                makeTransportFactory(factory), true);

  resolver.resolve("www.example.com");
  auto firstTransport = factory.transports.back();
  firstTransport->selectedAlpnProtocol = HTTP_ALPN_H2;
  driveUntilWriteRequestDone(resolver, factory);

  http2test::FakeHttp2ServerSession server;
  server.feedInboundData(firstTransport->written);
  server.submitRstStream(1, NGHTTP2_REFUSED_STREAM);
  firstTransport->readBuffer = server.drainOutboundData();

  for (size_t i = 0; i < 8 && factory.transports.size() == 1; ++i) {
    driveOnce(resolver, firstTransport);
  }
  CPPUNIT_ASSERT_EQUAL((size_t)2, factory.transports.size());
  auto secondTransport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("8.8.8.8"),
                       secondTransport->connectHost);

  driveUntilWriteRequestDone(resolver, factory);
  auto response = createAResponse(getQueryId(secondTransport->written),
                                  "www.example.com");
  secondTransport->readBuffer = createHttpResponse(response);
  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.9"),
                       resolver.getResolvedAddresses()[0]);
}
#endif // HAVE_LIBNGHTTP2

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

void AsyncDohNameResolverTest::testDomainServerUsesBootstrapAddress()
{
  FakeDohTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDohNameResolver resolver(
      AF_INET, {{"dns.example.org", 443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory), false,
      makeBootstrapResolverFactory(bootstrapFactory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL((size_t)1, bootstrapFactory.hostnames.size());
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       bootstrapFactory.hostnames[0]);
  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       getHeaderValue(transport->written, "Host: "));
}

void AsyncDohNameResolverTest::testDomainServerUsesAsyncBootstrapAddress()
{
  FakeDohTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_QUERYING);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDohNameResolver resolver(
      AF_INET, {{"dns.example.org", 443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory), false,
      makeBootstrapResolverFactory(bootstrapFactory), AF_INET);

  resolver.resolve("www.example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDohNameResolver::DOH_BOOTSTRAP_RESOLVING,
                       resolver.getDohState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, bootstrapFactory.hostnames.size());
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       bootstrapFactory.hostnames[0]);
  CPPUNIT_ASSERT_EQUAL((size_t)1, bootstrapFactory.families.size());
  CPPUNIT_ASSERT_EQUAL(AF_INET, bootstrapFactory.families[0]);
  CPPUNIT_ASSERT(factory.transports.empty());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_READ,
                       resolver.getsock()[0].events);

  resolver.process(resolver.getsock()[0].fd, AsyncResolver::badSocket());

  CPPUNIT_ASSERT_EQUAL((size_t)1, factory.transports.size());
  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL(transport->getSocket(), resolver.getsock()[0].fd);
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);
  driveUntilWriteRequestDone(resolver, factory);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       getHeaderValue(transport->written, "Host: "));
}

void AsyncDohNameResolverTest::testDomainServerRetriesNextBootstrapAddress()
{
  FakeDohTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"198.51.100.1", "203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDohNameResolver resolver(
      AF_INET, {{"dns.example.org", 443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory), false,
      makeBootstrapResolverFactory(bootstrapFactory));

  resolver.resolve("www.example.com");
  CPPUNIT_ASSERT_EQUAL((size_t)1, factory.transports.size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.1"),
                       factory.transports.back()->connectHost);

  driveOnce(resolver, factory.transports.back());
  CPPUNIT_ASSERT_EQUAL((size_t)2, factory.transports.size());
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"),
                       factory.transports.back()->connectHost);
}

void AsyncDohNameResolverTest::testDomainServerRetriesNextServerOnBootstrapError()
{
  FakeDohTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back(std::vector<std::string>());
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_ERROR);
  bootstrapFactory.errors.push_back("NXDOMAIN");
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDohNameResolver resolver(
      AF_INET,
      {{"bad.example.org", 443, "bad.example.org", "/dns-query"},
       {"dns.example.org", 443, "dns.example.org", "/dns-query"}},
      makeTransportFactory(factory), false,
      makeBootstrapResolverFactory(bootstrapFactory));

  resolver.resolve("www.example.com");
  driveUntilWriteRequestDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL((size_t)2, bootstrapFactory.hostnames.size());
  CPPUNIT_ASSERT_EQUAL(std::string("bad.example.org"),
                       bootstrapFactory.hostnames[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       bootstrapFactory.hostnames[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"),
                       factory.transports.back()->connectHost);
}

} // namespace aria2

#endif // ENABLE_SSL
