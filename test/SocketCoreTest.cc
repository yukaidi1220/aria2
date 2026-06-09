#include "SocketCore.h"

#include <cstring>
#include <iostream>
#include <cppunit/extensions/HelperMacros.h>

#include "a2functional.h"
#include "Exception.h"
#ifdef ENABLE_SSL
#  include "TLSSession.h"
#  ifdef HAVE_OPENSSL
#    include "LibsslTLSSession.h"
#  endif // HAVE_OPENSSL
#  ifdef HAVE_LIBGNUTLS
#    include "LibgnutlsTLSSession.h"
#  endif // HAVE_LIBGNUTLS
#endif // ENABLE_SSL

namespace aria2 {

class SocketCoreTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(SocketCoreTest);
  CPPUNIT_TEST(testWriteAndReadDatagram);
  CPPUNIT_TEST(testGetSocketError);
  CPPUNIT_TEST(testInetNtop);
  CPPUNIT_TEST(testInetPton);
  CPPUNIT_TEST(testGetNumericAddressFamily);
  CPPUNIT_TEST(testIsIPv6GlobalUnicastAddress);
  CPPUNIT_TEST(testGetBinAddr);
  CPPUNIT_TEST(testVerifyHostname);
#ifdef ENABLE_SSL
  CPPUNIT_TEST(testTLSHandshakeParams);
  CPPUNIT_TEST(testTLSHandshakeParamsComparison);
  CPPUNIT_TEST(testTLSHandshakeParamsOriginCoalescingCompatibility);
  CPPUNIT_TEST(testTLSSessionAlpnSupportDefault);
  CPPUNIT_TEST(testTLSSessionECHSupportDefault);
#ifdef HAVE_LIBGNUTLS
  CPPUNIT_TEST(testGnuTLSSessionAlpnSupport);
#endif // HAVE_LIBGNUTLS
#ifdef HAVE_OPENSSL
  CPPUNIT_TEST(testOpenSSLECHSupportDetection);
#endif // HAVE_OPENSSL
  CPPUNIT_TEST(testTLSSessionPeerCertificateMatchDefault);
  CPPUNIT_TEST(testMatchesTLSHandshakeParams);
  CPPUNIT_TEST(testIsTLSSNIHostname);
#endif // ENABLE_SSL
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}

  void tearDown() {}

  void testWriteAndReadDatagram();
  void testGetSocketError();
  void testInetNtop();
  void testInetPton();
  void testGetNumericAddressFamily();
  void testIsIPv6GlobalUnicastAddress();
  void testGetBinAddr();
  void testVerifyHostname();
#ifdef ENABLE_SSL
  void testTLSHandshakeParams();
  void testTLSHandshakeParamsComparison();
  void testTLSHandshakeParamsOriginCoalescingCompatibility();
  void testTLSSessionAlpnSupportDefault();
  void testTLSSessionECHSupportDefault();
#ifdef HAVE_LIBGNUTLS
  void testGnuTLSSessionAlpnSupport();
#endif // HAVE_LIBGNUTLS
#ifdef HAVE_OPENSSL
  void testOpenSSLECHSupportDetection();
#endif // HAVE_OPENSSL
  void testTLSSessionPeerCertificateMatchDefault();
  void testMatchesTLSHandshakeParams();
  void testIsTLSSNIHostname();
#endif // ENABLE_SSL
};

CPPUNIT_TEST_SUITE_REGISTRATION(SocketCoreTest);

void SocketCoreTest::testWriteAndReadDatagram()
{
  try {
    SocketCore s(SOCK_DGRAM);
    s.bind(0);
    SocketCore c(SOCK_DGRAM);
    c.bind(0);

    auto remoteEndpoint = s.getAddrInfo();

    std::string message1 = "hello world.";
    c.writeData(message1.c_str(), message1.size(), "localhost",
                remoteEndpoint.port);
    std::string message2 = "chocolate coated pie";
    c.writeData(message2.c_str(), message2.size(), "localhost",
                remoteEndpoint.port);

    char readbuffer[100];

    {
      ssize_t rlength =
          s.readDataFrom(readbuffer, sizeof(readbuffer), remoteEndpoint);
      // commented out because ip address may vary
      // CPPUNIT_ASSERT_EQUAL(std::std::string("127.0.0.1"),
      //                      remoteEndpoint.addr);
      CPPUNIT_ASSERT_EQUAL((ssize_t)message1.size(), rlength);
      readbuffer[rlength] = '\0';
      CPPUNIT_ASSERT_EQUAL(message1, std::string(readbuffer));
    }
    {
      ssize_t rlength =
          s.readDataFrom(readbuffer, sizeof(readbuffer), remoteEndpoint);
      CPPUNIT_ASSERT_EQUAL((ssize_t)message2.size(), rlength);
      readbuffer[rlength] = '\0';
      CPPUNIT_ASSERT_EQUAL(message2, std::string(readbuffer));
    }
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace() << std::endl;
    CPPUNIT_FAIL("exception thrown");
  }
}

void SocketCoreTest::testGetSocketError()
{
  SocketCore s;
  s.bind(0);
  // See there is no error at this point
  CPPUNIT_ASSERT_EQUAL(std::string(""), s.getSocketError());
}

void SocketCoreTest::testInetNtop()
{
  char dest[NI_MAXHOST];
  {
    std::string s = "192.168.0.1";
    addrinfo* res;
    CPPUNIT_ASSERT_EQUAL(0, callGetaddrinfo(&res, s.c_str(), nullptr, AF_INET,
                                            SOCK_STREAM, 0, 0));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                  freeaddrinfo);
    sockaddr_in addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    CPPUNIT_ASSERT_EQUAL(0,
                         inetNtop(AF_INET, &addr.sin_addr, dest, sizeof(dest)));
    CPPUNIT_ASSERT_EQUAL(s, std::string(dest));
  }
  {
    std::string s = "2001:db8::2:1";
    addrinfo* res;
    CPPUNIT_ASSERT_EQUAL(0, callGetaddrinfo(&res, s.c_str(), nullptr, AF_INET6,
                                            SOCK_STREAM, 0, 0));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                  freeaddrinfo);
    sockaddr_in6 addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    CPPUNIT_ASSERT_EQUAL(
        0, inetNtop(AF_INET6, &addr.sin6_addr, dest, sizeof(dest)));
    CPPUNIT_ASSERT_EQUAL(s, std::string(dest));
  }
}

void SocketCoreTest::testInetPton()
{
  {
    const char ipaddr[] = "192.168.0.1";
    in_addr ans;
    CPPUNIT_ASSERT_EQUAL((size_t)4, net::getBinAddr(&ans, ipaddr));
    in_addr dest;
    CPPUNIT_ASSERT_EQUAL(0, inetPton(AF_INET, ipaddr, &dest));
    CPPUNIT_ASSERT(memcmp(&ans, &dest, sizeof(ans)) == 0);
  }
  {
    const char ipaddr[] = "2001:db8::2:1";
    in6_addr ans;
    CPPUNIT_ASSERT_EQUAL((size_t)16, net::getBinAddr(&ans, ipaddr));
    in6_addr dest;
    CPPUNIT_ASSERT_EQUAL(0, inetPton(AF_INET6, ipaddr, &dest));
    CPPUNIT_ASSERT(memcmp(&ans, &dest, sizeof(ans)) == 0);
  }
  unsigned char dest[16];
  CPPUNIT_ASSERT_EQUAL(-1, inetPton(AF_INET, "localhost", &dest));
  CPPUNIT_ASSERT_EQUAL(-1, inetPton(AF_INET6, "localhost", &dest));
}

void SocketCoreTest::testGetNumericAddressFamily()
{
  CPPUNIT_ASSERT_EQUAL(AF_INET, getNumericAddressFamily("192.0.2.1"));
  CPPUNIT_ASSERT_EQUAL(AF_INET6, getNumericAddressFamily("2001:db8::1"));
  CPPUNIT_ASSERT_EQUAL(AF_INET6, getNumericAddressFamily("fd00::1"));
  CPPUNIT_ASSERT_EQUAL(0, getNumericAddressFamily("localhost"));
}

void SocketCoreTest::testIsIPv6GlobalUnicastAddress()
{
  CPPUNIT_ASSERT(isIPv6GlobalUnicastAddress("2001:db8::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("fd00::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("fc00::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("fe80::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("fec0::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("::"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("::ffff:192.0.2.1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("::192.0.2.1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("ff02::1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("192.0.2.1"));
  CPPUNIT_ASSERT(!isIPv6GlobalUnicastAddress("localhost"));
}

void SocketCoreTest::testGetBinAddr()
{
  unsigned char dest[16];
  unsigned char ans1[] = {192, 168, 0, 1};
  CPPUNIT_ASSERT_EQUAL((size_t)4, net::getBinAddr(dest, "192.168.0.1"));
  CPPUNIT_ASSERT(std::equal(&dest[0], &dest[4], &ans1[0]));

  unsigned char ans2[] = {0x20u, 0x01u, 0x0du, 0xb8u, 0x00u, 0x00u,
                          0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                          0x00u, 0x02u, 0x00u, 0x01u};
  CPPUNIT_ASSERT_EQUAL((size_t)16, net::getBinAddr(dest, "2001:db8::2:1"));
  CPPUNIT_ASSERT(std::equal(&dest[0], &dest[16], &ans2[0]));

  CPPUNIT_ASSERT_EQUAL((size_t)0, net::getBinAddr(dest, "localhost"));
}

void SocketCoreTest::testVerifyHostname()
{
  {
    std::vector<std::string> dnsNames, ipAddrs;
    std::string commonName;
    CPPUNIT_ASSERT(
        !net::verifyHostname("example.org", dnsNames, ipAddrs, commonName));
  }
  {
    // Only commonName is provided
    std::vector<std::string> dnsNames, ipAddrs;
    std::string commonName = "example.org";
    CPPUNIT_ASSERT(
        net::verifyHostname("example.org", dnsNames, ipAddrs, commonName));
  }
  {
    // Match against dNSName in subjectAltName
    std::vector<std::string> dnsNames, ipAddrs;
    dnsNames.push_back("foo");
    dnsNames.push_back("example.org");
    std::string commonName = "exampleX.org";
    CPPUNIT_ASSERT(
        net::verifyHostname("example.org", dnsNames, ipAddrs, commonName));
  }
  {
    // If dNsName is provided, don't match with commonName
    std::vector<std::string> dnsNames, ipAddrs;
    dnsNames.push_back("foo");
    dnsNames.push_back("exampleX.org");
    ipAddrs.push_back("example.org");
    std::string commonName = "example.org";
    CPPUNIT_ASSERT(
        !net::verifyHostname("example.org", dnsNames, ipAddrs, commonName));
  }
  {
    // IPAddress in dnsName don't match.
    std::vector<std::string> dnsNames, ipAddrs;
    dnsNames.push_back("192.168.0.1");
    std::string commonName = "example.org";
    CPPUNIT_ASSERT(
        !net::verifyHostname("192.168.0.1", dnsNames, ipAddrs, commonName));
  }
  {
    // IPAddress string match with commonName
    std::vector<std::string> dnsNames, ipAddrs;
    std::string commonName = "192.168.0.1";
    CPPUNIT_ASSERT(
        net::verifyHostname("192.168.0.1", dnsNames, ipAddrs, commonName));
  }
  {
    // Match against iPAddress in subjectAltName
    std::vector<std::string> dnsNames, ipAddrs;
    unsigned char binAddr[16];
    size_t len;
    len = net::getBinAddr(binAddr, "192.168.0.1");
    ipAddrs.push_back(std::string(binAddr, binAddr + len));
    std::string commonName = "example.org";
    CPPUNIT_ASSERT(
        net::verifyHostname("192.168.0.1", dnsNames, ipAddrs, commonName));
  }
  {
    // Match against iPAddress (ipv6) in subjectAltName
    std::vector<std::string> dnsNames, ipAddrs;
    unsigned char binAddr[16];
    size_t len;
    len = net::getBinAddr(binAddr, "::1");
    ipAddrs.push_back(std::string(binAddr, binAddr + len));
    std::string commonName = "example.org";
    CPPUNIT_ASSERT(net::verifyHostname("::1", dnsNames, ipAddrs, commonName));
  }
  {
    // If iPAddress is provided, don't match with commonName
    std::vector<std::string> dnsNames, ipAddrs;
    unsigned char binAddr[16];
    size_t len;
    len = net::getBinAddr(binAddr, "192.168.0.2");
    ipAddrs.push_back(std::string(binAddr, binAddr + len));
    std::string commonName = "192.168.0.1";
    CPPUNIT_ASSERT(
        !net::verifyHostname("192.168.0.1", dnsNames, ipAddrs, commonName));
  }
}

#ifdef ENABLE_SSL
void SocketCoreTest::testTLSHandshakeParams()
{
  {
    TLSHandshakeParams params;
    CPPUNIT_ASSERT(params.sniHost.empty());
    CPPUNIT_ASSERT(params.verifyHost.empty());
    CPPUNIT_ASSERT(params.alpnProtocols.empty());
    CPPUNIT_ASSERT(!params.echParams.requested);
    CPPUNIT_ASSERT(!params.echParams.required);
    CPPUNIT_ASSERT(params.echParams.configList.empty());
    CPPUNIT_ASSERT(params.echParams.source.empty());
    CPPUNIT_ASSERT(params.echParams.outerName.empty());
    CPPUNIT_ASSERT(params.echParams.outerAlpnProtocols.empty());
    CPPUNIT_ASSERT(params.echParams.retryConfigList.empty());
    CPPUNIT_ASSERT(!params.sniHostOverridden);
  }
  {
    TLSHandshakeParams params("example.org");
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.sniHost);
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.verifyHost);
    CPPUNIT_ASSERT(params.alpnProtocols.empty());
    CPPUNIT_ASSERT(!params.sniHostOverridden);
  }
  {
    TLSHandshakeParams params("front.example", "origin.example");
    CPPUNIT_ASSERT_EQUAL(std::string("front.example"), params.sniHost);
    CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
    CPPUNIT_ASSERT(params.alpnProtocols.empty());
    CPPUNIT_ASSERT(params.sniHostOverridden);
  }
  {
    TLSHandshakeParams params("example.org", "example.org", true);
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.sniHost);
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.verifyHost);
    CPPUNIT_ASSERT(params.sniHostOverridden);
  }
  {
    std::vector<std::string> alpnProtocols;
    alpnProtocols.push_back("h2");
    alpnProtocols.push_back("http/1.1");
    TLSHandshakeParams params("front.example", "origin.example",
                              alpnProtocols);
    CPPUNIT_ASSERT_EQUAL(std::string("front.example"), params.sniHost);
    CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), params.verifyHost);
    CPPUNIT_ASSERT_EQUAL((size_t)2, params.alpnProtocols.size());
    CPPUNIT_ASSERT_EQUAL(std::string("h2"), params.alpnProtocols[0]);
    CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), params.alpnProtocols[1]);
    CPPUNIT_ASSERT(params.sniHostOverridden);
  }
  {
    TLSHandshakeParams params("front.example", "origin.example");
    params.echParams.requested = true;
    params.echParams.required = true;
    params.echParams.configList = "ech-config";
    params.echParams.source = "manual";
    params.echParams.outerName = "public.example";
    params.echParams.outerAlpnProtocols.push_back("h2");
    params.echParams.retryConfigList = "retry-config";
    CPPUNIT_ASSERT(params.echParams.requested);
    CPPUNIT_ASSERT(params.echParams.required);
    CPPUNIT_ASSERT_EQUAL(std::string("ech-config"),
                         params.echParams.configList);
    CPPUNIT_ASSERT_EQUAL(std::string("manual"), params.echParams.source);
    CPPUNIT_ASSERT_EQUAL(std::string("public.example"),
                         params.echParams.outerName);
    CPPUNIT_ASSERT_EQUAL((size_t)1,
                         params.echParams.outerAlpnProtocols.size());
    CPPUNIT_ASSERT_EQUAL(std::string("h2"),
                         params.echParams.outerAlpnProtocols[0]);
    CPPUNIT_ASSERT_EQUAL(std::string("retry-config"),
                         params.echParams.retryConfigList);
  }
  {
    std::vector<std::string> alpnProtocols;
    alpnProtocols.push_back("http/1.1");
    TLSHandshakeParams params("example.org", "example.org", alpnProtocols,
                              true);
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.sniHost);
    CPPUNIT_ASSERT_EQUAL(std::string("example.org"), params.verifyHost);
    CPPUNIT_ASSERT_EQUAL((size_t)1, params.alpnProtocols.size());
    CPPUNIT_ASSERT(params.sniHostOverridden);
  }
}

void SocketCoreTest::testTLSHandshakeParamsComparison()
{
  TLSHandshakeParams base("example.org");
  CPPUNIT_ASSERT(base == TLSHandshakeParams("example.org"));
  CPPUNIT_ASSERT(!(base != TLSHandshakeParams("example.org")));
  CPPUNIT_ASSERT(base != TLSHandshakeParams("front.example", "example.org"));
  CPPUNIT_ASSERT(base != TLSHandshakeParams("example.org", "example.org",
                                            true));

  std::vector<std::string> alpnProtocols;
  alpnProtocols.push_back("h2");
  alpnProtocols.push_back("http/1.1");
  TLSHandshakeParams withAlpn("example.org", "example.org", alpnProtocols);
  CPPUNIT_ASSERT(withAlpn == TLSHandshakeParams("example.org", "example.org",
                                                alpnProtocols));
  CPPUNIT_ASSERT(withAlpn != base);

  std::vector<std::string> reversedAlpnProtocols;
  reversedAlpnProtocols.push_back("http/1.1");
  reversedAlpnProtocols.push_back("h2");
  CPPUNIT_ASSERT(withAlpn != TLSHandshakeParams("example.org", "example.org",
                                                reversedAlpnProtocols));

  TLSHandshakeParams withEch("example.org");
  withEch.echParams.requested = true;
  withEch.echParams.configList = "ech-config";
  CPPUNIT_ASSERT(withEch == withEch);
  CPPUNIT_ASSERT(withEch != base);

  TLSHandshakeParams withDifferentEch("example.org");
  withDifferentEch.echParams.requested = true;
  withDifferentEch.echParams.configList = "other-ech-config";
  CPPUNIT_ASSERT(withEch != withDifferentEch);
}

void SocketCoreTest::testTLSHandshakeParamsOriginCoalescingCompatibility()
{
  std::vector<std::string> alpnProtocols;
  alpnProtocols.push_back("h2");
  alpnProtocols.push_back("http/1.1");

  TLSHandshakeParams established("origin.example", "origin.example",
                                 alpnProtocols);
  TLSHandshakeParams candidate("cdn.example", "cdn.example", alpnProtocols);
  CPPUNIT_ASSERT(established != candidate);
  CPPUNIT_ASSERT(tlsHandshakeParamsCompatibleForOriginCoalescing(established,
                                                                candidate));

  std::vector<std::string> http11OnlyProtocols;
  http11OnlyProtocols.push_back("http/1.1");
  TLSHandshakeParams differentAlpn("cdn.example", "cdn.example",
                                   http11OnlyProtocols);
  CPPUNIT_ASSERT(!tlsHandshakeParamsCompatibleForOriginCoalescing(
      established, differentAlpn));

  TLSHandshakeParams overriddenEstablished("front.example", "origin.example",
                                           alpnProtocols, true);
  CPPUNIT_ASSERT(!tlsHandshakeParamsCompatibleForOriginCoalescing(
      overriddenEstablished, candidate));

  TLSHandshakeParams overriddenCandidate("front.example", "cdn.example",
                                         alpnProtocols, true);
  CPPUNIT_ASSERT(!tlsHandshakeParamsCompatibleForOriginCoalescing(
      established, overriddenCandidate));

  TLSHandshakeParams echEstablished("origin.example", "origin.example",
                                    alpnProtocols);
  echEstablished.echParams.requested = true;
  echEstablished.echParams.configList = "ech-config";

  TLSHandshakeParams echCandidate("cdn.example", "cdn.example",
                                  alpnProtocols);
  echCandidate.echParams.requested = true;
  echCandidate.echParams.configList = "ech-config";
  CPPUNIT_ASSERT(tlsHandshakeParamsCompatibleForOriginCoalescing(
      echEstablished, echCandidate));

  TLSHandshakeParams differentEchCandidate("cdn.example", "cdn.example",
                                           alpnProtocols);
  differentEchCandidate.echParams.requested = true;
  differentEchCandidate.echParams.configList = "other-ech-config";
  CPPUNIT_ASSERT(!tlsHandshakeParamsCompatibleForOriginCoalescing(
      echEstablished, differentEchCandidate));
}

namespace {
class DefaultTLSSession : public TLSSession {
public:
  virtual int init(sock_t sockfd) CXX11_OVERRIDE
  {
    (void)sockfd;
    return TLS_ERR_OK;
  }
  virtual int setSNIHostname(const std::string& hostname) CXX11_OVERRIDE
  {
    (void)hostname;
    return TLS_ERR_OK;
  }
  virtual int closeConnection() CXX11_OVERRIDE { return TLS_ERR_OK; }
  virtual int checkDirection() CXX11_OVERRIDE { return TLS_WANT_READ; }
  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE
  {
    (void)data;
    return len;
  }
  virtual ssize_t readData(void* data, size_t len) CXX11_OVERRIDE
  {
    (void)data;
    (void)len;
    return TLS_ERR_WOULDBLOCK;
  }
  virtual int tlsConnect(const std::string& hostname, TLSVersion& version,
                         std::string& handshakeError) CXX11_OVERRIDE
  {
    (void)hostname;
    (void)handshakeError;
    version = TLS_PROTO_TLS12;
    return TLS_ERR_OK;
  }
  virtual int tlsAccept(TLSVersion& version) CXX11_OVERRIDE
  {
    version = TLS_PROTO_TLS12;
    return TLS_ERR_OK;
  }
  virtual std::string getLastErrorString() CXX11_OVERRIDE
  {
    return std::string();
  }
  virtual size_t getRecvBufferedLength() CXX11_OVERRIDE { return 0; }
};
} // namespace

void SocketCoreTest::testTLSSessionAlpnSupportDefault()
{
  DefaultTLSSession session;
  std::vector<std::string> alpnProtocols;
  alpnProtocols.push_back("h2");
  alpnProtocols.push_back("http/1.1");

  CPPUNIT_ASSERT(!session.supportsAlpnProtocols());
  CPPUNIT_ASSERT_EQUAL(TLS_ERR_OK,
                       session.setAlpnProtocols(std::vector<std::string>()));
  CPPUNIT_ASSERT_EQUAL(TLS_ERR_ERROR, session.setAlpnProtocols(alpnProtocols));
}

void SocketCoreTest::testTLSSessionECHSupportDefault()
{
  DefaultTLSSession session;

  CPPUNIT_ASSERT(!session.supportsECHConfigList());
  CPPUNIT_ASSERT_EQUAL(TLS_ERR_OK, session.setECHConfigList(""));
  CPPUNIT_ASSERT_EQUAL(TLS_ERR_ERROR,
                       session.setECHConfigList("ech-config"));
  CPPUNIT_ASSERT(TLS_ECH_STATUS_NOT_CONFIGURED == session.getECHStatus());
  CPPUNIT_ASSERT(session.getECHRetryConfigList().empty());
}

#ifdef HAVE_LIBGNUTLS
void SocketCoreTest::testGnuTLSSessionAlpnSupport()
{
  GnuTLSSession session(nullptr);
#if defined(HAVE_GNUTLS_ALPN_SET_PROTOCOLS) &&                             \
    defined(HAVE_GNUTLS_ALPN_GET_SELECTED_PROTOCOL)
  CPPUNIT_ASSERT(session.supportsAlpnProtocols());
#else  // !(HAVE_GNUTLS_ALPN_SET_PROTOCOLS && HAVE_GNUTLS_ALPN_GET_SELECTED_PROTOCOL)
  CPPUNIT_ASSERT(!session.supportsAlpnProtocols());
#endif // !(HAVE_GNUTLS_ALPN_SET_PROTOCOLS && HAVE_GNUTLS_ALPN_GET_SELECTED_PROTOCOL)
}
#endif // HAVE_LIBGNUTLS

#ifdef HAVE_OPENSSL
void SocketCoreTest::testOpenSSLECHSupportDetection()
{
  OpenSSLTLSSession session(nullptr);
#if defined(HAVE_OPENSSL_ECH_H) &&                                         \
    defined(HAVE_DECL_SSL_SET1_ECH_CONFIG_LIST) &&                         \
    HAVE_DECL_SSL_SET1_ECH_CONFIG_LIST &&                                  \
    defined(HAVE_SSL_SET1_ECH_CONFIG_LIST)
  CPPUNIT_ASSERT(session.supportsECHConfigList());
#else  // !OpenSSL ECH API
  CPPUNIT_ASSERT(!session.supportsECHConfigList());
#endif // !OpenSSL ECH API
}
#endif // HAVE_OPENSSL

void SocketCoreTest::testTLSSessionPeerCertificateMatchDefault()
{
  DefaultTLSSession session;

  CPPUNIT_ASSERT(!session.peerCertificateMatchesHostname("example.org"));
}

void SocketCoreTest::testMatchesTLSHandshakeParams()
{
  SocketCore socket;
  CPPUNIT_ASSERT(!socket.matchesTLSHandshakeParams(
      TLSHandshakeParams("example.org")));
  CPPUNIT_ASSERT(!socket.matchesTLSHandshakeParamsForOriginCoalescing(
      TLSHandshakeParams("example.org")));
}

void SocketCoreTest::testIsTLSSNIHostname()
{
  CPPUNIT_ASSERT(isTLSSNIHostname("example.org"));
  CPPUNIT_ASSERT(isTLSSNIHostname("front.example"));
  CPPUNIT_ASSERT(isTLSSNIHostname("a-b.example"));
  CPPUNIT_ASSERT(isTLSSNIHostname("EXAMPLE.ORG"));
  CPPUNIT_ASSERT(!isTLSSNIHostname(""));
  CPPUNIT_ASSERT(!isTLSSNIHostname("localhost"));
  CPPUNIT_ASSERT(!isTLSSNIHostname(".example"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("example."));
  CPPUNIT_ASSERT(!isTLSSNIHostname("example..org"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("-example.org"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("example-.org"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("bad_name.example"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("192.168.0.1"));
  CPPUNIT_ASSERT(!isTLSSNIHostname("2001:db8::1"));
}
#endif // ENABLE_SSL

} // namespace aria2
