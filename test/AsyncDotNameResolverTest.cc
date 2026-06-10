#include "AsyncDotNameResolver.h"

#ifdef ENABLE_SSL

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "AsyncDnsServerConfig.h"
#include "DnsMessage.h"
#include "EventPoll.h"
#include "File.h"
#include "LogFactory.h"
#include "Logger.h"
#include "TestUtil.h"
#include "a2functional.h"
#include "prefs.h"

namespace aria2 {

class AsyncDotNameResolverTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AsyncDotNameResolverTest);
  CPPUNIT_TEST(testResolveAResponseWithFragmentedRead);
  CPPUNIT_TEST(testResolveAAAAResponse);
  CPPUNIT_TEST(testResolveHttpsServiceBindingResponse);
  CPPUNIT_TEST(testNumericServerUsesAddressForHandshake);
  CPPUNIT_TEST(testNumericIPv6ServerUsesAddressForHandshake);
  CPPUNIT_TEST(testTlsWantReadUpdatesSocketEvents);
  CPPUNIT_TEST(testTlsWantWriteUpdatesSocketEvents);
  CPPUNIT_TEST(testWriteWantWriteThenSucceeds);
  CPPUNIT_TEST(testProcessTimeoutDrainsBufferedReadData);
  CPPUNIT_TEST(testProcessTimeoutDoesNotFailQuery);
  CPPUNIT_TEST(testRejectEmptyServerList);
  CPPUNIT_TEST(testRejectInvalidHostname);
  CPPUNIT_TEST(testBadResponseLengthFails);
  CPPUNIT_TEST(testResponseIdMismatchFails);
  CPPUNIT_TEST(testRetryNextServerOnConnectError);
  CPPUNIT_TEST(testDomainServerUsesBootstrapAddress);
  CPPUNIT_TEST(testDomainServerUsesAsyncBootstrapAddress);
  CPPUNIT_TEST(testDomainServerRetriesNextBootstrapAddress);
  CPPUNIT_TEST(testDomainServerRetriesNextServerOnBootstrapError);
  CPPUNIT_TEST_SUITE_END();

public:
  void testResolveAResponseWithFragmentedRead();
  void testResolveAAAAResponse();
  void testResolveHttpsServiceBindingResponse();
  void testNumericServerUsesAddressForHandshake();
  void testNumericIPv6ServerUsesAddressForHandshake();
  void testTlsWantReadUpdatesSocketEvents();
  void testTlsWantWriteUpdatesSocketEvents();
  void testWriteWantWriteThenSucceeds();
  void testProcessTimeoutDrainsBufferedReadData();
  void testProcessTimeoutDoesNotFailQuery();
  void testRejectEmptyServerList();
  void testRejectInvalidHostname();
  void testBadResponseLengthFails();
  void testResponseIdMismatchFails();
  void testRetryNextServerOnConnectError();
  void testDomainServerUsesBootstrapAddress();
  void testDomainServerUsesAsyncBootstrapAddress();
  void testDomainServerRetriesNextBootstrapAddress();
  void testDomainServerRetriesNextServerOnBootstrapError();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncDotNameResolverTest);

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

uint16_t readUint16(const std::string& s, size_t offset)
{
  return (static_cast<uint16_t>(static_cast<unsigned char>(s[offset])) << 8) |
         static_cast<uint16_t>(static_cast<unsigned char>(s[offset + 1]));
}

uint16_t getQueryId(const std::string& dotQuery)
{
  return readUint16(dotQuery, 2);
}

std::string getDotQueryBody(const std::string& dotQuery)
{
  return dotQuery.substr(2);
}

uint16_t getQuestionType(const std::string& dnsMessage)
{
  size_t offset = 12;
  while (offset < dnsMessage.size() && dnsMessage[offset]) {
    offset += static_cast<unsigned char>(dnsMessage[offset]) + 1;
  }
  ++offset;
  return readUint16(dnsMessage, offset);
}

std::string createAResponse(uint16_t id, const std::string& hostname)
{
  std::string msg;
  appendHeader(msg, id, 0x8180, 1, 1);
  appendQuestion(msg, hostname, dns::TYPE_A);
  appendAAnswer(msg, 12, 60, 198, 51, 100, 7);
  return msg;
}

std::string createAAAAResponse(uint16_t id, const std::string& hostname)
{
  static const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 1};
  std::string msg;
  appendHeader(msg, id, 0x8180, 1, 1);
  appendQuestion(msg, hostname, dns::TYPE_AAAA);
  appendAAAAAnswer(msg, 12, 60, addr);
  return msg;
}

std::string createHttpsResponse(uint16_t id, const std::string& hostname)
{
  std::string msg;
  appendHeader(msg, id, 0x8180, 1, 1);
  appendQuestion(msg, hostname, dns::TYPE_HTTPS);
  appendCompressedName(msg, 12);
  appendUint16(msg, dns::TYPE_HTTPS);
  appendUint16(msg, 1);
  appendUint32(msg, 120);
  appendUint16(msg, 10);
  appendUint16(msg, 1);
  msg.push_back(0);
  appendUint16(msg, 1);
  appendUint16(msg, 3);
  msg.push_back(2);
  msg += "h2";
  return msg;
}

std::string createDotFrame(const std::string& dnsMessage)
{
  std::string frame;
  appendUint16(frame, static_cast<uint16_t>(dnsMessage.size()));
  frame += dnsMessage;
  return frame;
}

class FakeDotTransport : public AsyncDotTransport {
public:
  explicit FakeDotTransport(sock_t fd) : fd_(fd) {}

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

class FakeDotTransportFactory {
public:
  std::unique_ptr<AsyncDotTransport>
  operator()(const AsyncDnsServerConfig& server)
  {
    auto transport = make_unique<FakeDotTransport>(nextFd++);
    transports.push_back(transport.get());
    (void)server;
    std::unique_ptr<AsyncDotTransport> baseTransport(std::move(transport));
    return baseTransport;
  }

  sock_t nextFd = 500;
  std::vector<FakeDotTransport*> transports;
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
        fd_(600)
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

AsyncDotTransportFactory makeTransportFactory(FakeDotTransportFactory& factory)
{
  return [&factory](const AsyncDnsServerConfig& server) {
    return factory(server);
  };
}

AsyncDotBootstrapResolverFactory
makeBootstrapResolverFactory(FakeBootstrapResolverFactory& factory)
{
  return [&factory](int family) { return factory(family); };
}

void driveOnce(AsyncDotNameResolver& resolver, FakeDotTransport* transport)
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

void driveUntilWriteQueryDone(AsyncDotNameResolver& resolver,
                              FakeDotTransportFactory& factory)
{
  for (size_t i = 0; i < 8; ++i) {
    auto transport = factory.transports.back();
    driveOnce(resolver, transport);
    if (!transport->written.empty()) {
      return;
    }
  }
  CPPUNIT_FAIL("DoT query was not written");
}

void driveUntilDone(AsyncDotNameResolver& resolver,
                    FakeDotTransportFactory& factory)
{
  for (size_t i = 0; i < 256; ++i) {
    if (resolver.getStatus() == AsyncResolver::STATUS_SUCCESS ||
        resolver.getStatus() == AsyncResolver::STATUS_ERROR) {
      return;
    }
    driveOnce(resolver, factory.transports.back());
  }
  CPPUNIT_FAIL("DoT resolver did not finish");
}

} // namespace

void AsyncDotNameResolverTest::testResolveAResponseWithFragmentedRead()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_CONNECTING,
                       resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);

  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  CPPUNIT_ASSERT(transport->started);
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);

  auto queryLength =
      (static_cast<size_t>(static_cast<unsigned char>(transport->written[0]))
       << 8) |
      static_cast<size_t>(static_cast<unsigned char>(transport->written[1]));
  CPPUNIT_ASSERT_EQUAL(transport->written.size() - 2, queryLength);

  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createDotFrame(response);
  transport->maxReadSize = 1;

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_DONE, resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.7"),
                       resolver.getResolvedAddresses()[0]);
  CPPUNIT_ASSERT(resolver.getsock().empty());
}

void AsyncDotNameResolverTest::testResolveAAAAResponse()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET6, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  transport->readBuffer = createDotFrame(createAAAAResponse(
      getQueryId(transport->written), "www.example.com"));

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       resolver.getResolvedAddresses()[0]);
}

void AsyncDotNameResolverTest::testResolveHttpsServiceBindingResponse()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncDotNameResolverTest_testResolveHttpsServiceBindingResponse.log";
  ScopedNetworkLog log(logPath);
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolveHttpsServiceBinding("www.example.com", 8443);
  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  auto dnsQuery = getDotQueryBody(transport->written);
  CPPUNIT_ASSERT_EQUAL((uint16_t)dns::TYPE_HTTPS,
                       getQuestionType(dnsQuery));

  transport->readBuffer = createDotFrame(createHttpsResponse(
      getQueryId(transport->written), "_8443._https.www.example.com"));

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT(resolver.getResolvedAddresses().empty());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getServiceBindingRecords().size());
  const auto& record = resolver.getServiceBindingRecords()[0];
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, record.priority);
  CPPUNIT_ASSERT_EQUAL((uint32_t)120, record.ttl);
  CPPUNIT_ASSERT_EQUAL((size_t)1, record.alpn.size());
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), record.alpn[0]);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find(
                     "DNS: DoT HTTPS RR www.example.com returned 1 record(s) "
                     "qname=_8443._https.www.example.com "
                     "server=dot://203.0.113.8:853#dns.example.org "
                     "endpoint=203.0.113.8 transport=tls") !=
                 std::string::npos);
}

void AsyncDotNameResolverTest::testNumericServerUsesAddressForHandshake()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(AF_INET, {{"1.1.1.1", 8853, ""}},
                                makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8853, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT(transport->tlsParams.alpnProtocols.empty());
}

void AsyncDotNameResolverTest::testNumericIPv6ServerUsesAddressForHandshake()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"2606:4700:4700::1111", 853, ""}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);

  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, transport->connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       transport->tlsParams.verifyHost);
  CPPUNIT_ASSERT(transport->tlsParams.alpnProtocols.empty());
}

void AsyncDotNameResolverTest::testTlsWantReadUpdatesSocketEvents()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();
  transport->tlsBlocksWithRead = true;

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_TLS_HANDSHAKING,
                       resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_READ,
                       resolver.getsock()[0].events);
}

void AsyncDotNameResolverTest::testTlsWantWriteUpdatesSocketEvents()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();
  transport->tlsBlocksWithWrite = true;

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_TLS_HANDSHAKING,
                       resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);
}

void AsyncDotNameResolverTest::testWriteWantWriteThenSucceeds()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  auto transport = factory.transports.back();

  driveOnce(resolver, transport);
  driveOnce(resolver, transport);
  transport->writeBlocks = true;
  driveOnce(resolver, transport);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_WRITING_QUERY,
                       resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((int)EventPoll::EVENT_WRITE,
                       resolver.getsock()[0].events);
  CPPUNIT_ASSERT(transport->written.empty());

  driveOnce(resolver, transport);
  CPPUNIT_ASSERT(!transport->written.empty());
}

void AsyncDotNameResolverTest::testProcessTimeoutDrainsBufferedReadData()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response = createAResponse(getQueryId(transport->written),
                                  "www.example.com");
  transport->readBuffer = createDotFrame(response);
  transport->recvBufferedLength = 1;

  resolver.processTimeout();

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_DONE, resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.7"),
                       resolver.getResolvedAddresses()[0]);
  CPPUNIT_ASSERT(resolver.getsock().empty());
}

void AsyncDotNameResolverTest::testProcessTimeoutDoesNotFailQuery()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  resolver.processTimeout();

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_CONNECTING,
                       resolver.getDotState());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getsock().size());
}

void AsyncDotNameResolverTest::testRejectEmptyServerList()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(AF_INET, {}, makeTransportFactory(factory));

  resolver.resolve("www.example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_FAILED,
                       resolver.getDotState());
  CPPUNIT_ASSERT(!resolver.getError().empty());
  CPPUNIT_ASSERT(factory.transports.empty());
}

void AsyncDotNameResolverTest::testRejectInvalidHostname()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www..example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_FAILED,
                       resolver.getDotState());
  CPPUNIT_ASSERT(!resolver.getError().empty());
  CPPUNIT_ASSERT(factory.transports.empty());
}

void AsyncDotNameResolverTest::testBadResponseLengthFails()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);
  factory.transports.back()->readBuffer.assign(2, '\0');

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_FAILED,
                       resolver.getDotState());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDotNameResolverTest::testResponseIdMismatchFails()
{
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(
      AF_INET, {{"203.0.113.8", 853, "dns.example.org"}},
      makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  auto response =
      createAResponse(getQueryId(transport->written) + 1, "www.example.com");
  transport->readBuffer = createDotFrame(response);

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT(!resolver.getError().empty());
}

void AsyncDotNameResolverTest::testRetryNextServerOnConnectError()
{
  auto logPath =
      std::string(A2_TEST_OUT_DIR) +
      "/aria2_AsyncDotNameResolverTest_testRetryNextServerOnConnectError.log";
  ScopedNetworkLog log(logPath);
  FakeDotTransportFactory factory;
  AsyncDotNameResolver resolver(AF_INET,
                                {{"198.51.100.1", 853, "bad.example.org"},
                                 {"203.0.113.8", 853, "dns.example.org"}},
                                makeTransportFactory(factory));

  resolver.resolve("www.example.com");
  CPPUNIT_ASSERT_EQUAL((size_t)1, factory.transports.size());

  driveOnce(resolver, factory.transports.back());
  CPPUNIT_ASSERT_EQUAL((size_t)2, factory.transports.size());
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"),
                       factory.transports.back()->connectHost);

  driveUntilWriteQueryDone(resolver, factory);
  auto transport = factory.transports.back();
  transport->readBuffer = createDotFrame(createAResponse(
      getQueryId(transport->written), "www.example.com"));

  driveUntilDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.7"),
                       resolver.getResolvedAddresses()[0]);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: DoT connecting to "
                           "dot://198.51.100.1:853#bad.example.org via "
                           "198.51.100.1 for A www.example.com") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: DoT endpoint 198.51.100.1 for server "
                           "dot://198.51.100.1:853#bad.example.org failed: "
                           "DoT connection failed: connection refused") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: DoT server "
                           "dot://198.51.100.1:853#bad.example.org failed: "
                           "DoT connection failed: connection refused") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: DoT connecting to "
                           "dot://203.0.113.8:853#dns.example.org via "
                           "203.0.113.8 for A www.example.com") !=
                 std::string::npos);
}

void AsyncDotNameResolverTest::testDomainServerUsesBootstrapAddress()
{
  FakeDotTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDotNameResolver resolver(AF_INET,
                                {{"dns.example.org", 853, "dns.example.org"}},
                                makeTransportFactory(factory),
                                makeBootstrapResolverFactory(bootstrapFactory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);

  CPPUNIT_ASSERT_EQUAL((size_t)1, bootstrapFactory.hostnames.size());
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       bootstrapFactory.hostnames[0]);
  auto transport = factory.transports.back();
  CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.8"), transport->connectHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
}

void AsyncDotNameResolverTest::testDomainServerUsesAsyncBootstrapAddress()
{
  FakeDotTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_QUERYING);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDotNameResolver resolver(AF_INET,
                                {{"dns.example.org", 853, "dns.example.org"}},
                                makeTransportFactory(factory),
                                makeBootstrapResolverFactory(bootstrapFactory),
                                AF_INET);

  resolver.resolve("www.example.com");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_QUERYING, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(AsyncDotNameResolver::DOT_BOOTSTRAP_RESOLVING,
                       resolver.getDotState());
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
  driveUntilWriteQueryDone(resolver, factory);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.sniHost);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"),
                       transport->tlsParams.verifyHost);
}

void AsyncDotNameResolverTest::testDomainServerRetriesNextBootstrapAddress()
{
  FakeDotTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back({"198.51.100.1", "203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDotNameResolver resolver(AF_INET,
                                {{"dns.example.org", 853, "dns.example.org"}},
                                makeTransportFactory(factory),
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

void AsyncDotNameResolverTest::testDomainServerRetriesNextServerOnBootstrapError()
{
  FakeDotTransportFactory factory;
  FakeBootstrapResolverFactory bootstrapFactory;
  bootstrapFactory.results.push_back(std::vector<std::string>());
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_ERROR);
  bootstrapFactory.errors.push_back("NXDOMAIN");
  bootstrapFactory.results.push_back({"203.0.113.8"});
  bootstrapFactory.statuses.push_back(AsyncResolver::STATUS_SUCCESS);
  bootstrapFactory.errors.push_back(std::string());
  AsyncDotNameResolver resolver(AF_INET,
                                {{"bad.example.org", 853, "bad.example.org"},
                                 {"dns.example.org", 853, "dns.example.org"}},
                                makeTransportFactory(factory),
                                makeBootstrapResolverFactory(bootstrapFactory));

  resolver.resolve("www.example.com");
  driveUntilWriteQueryDone(resolver, factory);

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
