#include "Request.h"

#include <cppunit/extensions/HelperMacros.h>

#include "Netrc.h"
#include "DefaultAuthResolver.h"
#include "NetrcAuthResolver.h"
#include "uri.h"

namespace aria2 {

class RequestTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(RequestTest);
  CPPUNIT_TEST(testSetUri1);
  CPPUNIT_TEST(testSetUri2);
  CPPUNIT_TEST(testSetUri7);
  CPPUNIT_TEST(testSetUri_supportsPersistentConnection);
  CPPUNIT_TEST(testRedirectUri);
  CPPUNIT_TEST(testRedirectUri2);
  CPPUNIT_TEST(testRedirectUri_supportsPersistentConnection);
  CPPUNIT_TEST(testRedirectUri_uriNormalization);
  CPPUNIT_TEST(testResetUri);
  CPPUNIT_TEST(testResetUri_supportsPersistentConnection);
  CPPUNIT_TEST(testInnerLink);
  CPPUNIT_TEST(testInnerLinkInReferer);
  CPPUNIT_TEST(testGetURIHost);
  CPPUNIT_TEST(testConnectedAddrConfirmation);
  CPPUNIT_TEST(testResetConnectedAddrInfo);
  CPPUNIT_TEST(testHttp2OriginCoalescingState);
  CPPUNIT_TEST(testHttpsServiceBindingEndpointInfo);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSetUri1();
  void testSetUri2();
  void testSetUri7();
  void testSetUri_supportsPersistentConnection();
  void testRedirectUri();
  void testRedirectUri2();
  void testRedirectUri_supportsPersistentConnection();
  void testRedirectUri_uriNormalization();
  void testResetUri();
  void testResetUri_supportsPersistentConnection();
  void testInnerLink();
  void testInnerLinkInReferer();
  void testGetURIHost();
  void testConnectedAddrConfirmation();
  void testResetConnectedAddrInfo();
  void testHttp2OriginCoalescingState();
  void testHttpsServiceBindingEndpointInfo();
};

CPPUNIT_TEST_SUITE_REGISTRATION(RequestTest);

void RequestTest::testSetUri1()
{
  Request req;
  bool v = req.setUri("http://aria.rednoah.com/");

  CPPUNIT_ASSERT(v);
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/"), req.getUri());
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getReferer());
  CPPUNIT_ASSERT_EQUAL(std::string("http"), req.getProtocol());
  CPPUNIT_ASSERT_EQUAL((uint16_t)80, req.getPort());
  CPPUNIT_ASSERT_EQUAL(std::string("aria.rednoah.com"), req.getHost());
  CPPUNIT_ASSERT_EQUAL(std::string("/"), req.getDir());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getFile());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getQuery());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getUsername());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getPassword());
  CPPUNIT_ASSERT(!req.isIPv6LiteralAddress());
}

void RequestTest::testSetUri2()
{
  Request req;
  bool v = req.setUri("http://aria.rednoah.com:8080/index.html");
  req.setReferer("http://aria.rednoah.com:8080");

  CPPUNIT_ASSERT(v);

  // referer is unchanged
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com:8080"),
                       req.getReferer());
  CPPUNIT_ASSERT_EQUAL(std::string("http"), req.getProtocol());
  CPPUNIT_ASSERT_EQUAL((uint16_t)8080, req.getPort());
  CPPUNIT_ASSERT_EQUAL(std::string("aria.rednoah.com"), req.getHost());
  CPPUNIT_ASSERT_EQUAL(std::string("/"), req.getDir());
  CPPUNIT_ASSERT_EQUAL(std::string("index.html"), req.getFile());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getQuery());
}

void RequestTest::testSetUri7()
{
  Request req;
  bool v = req.setUri("http://");

  CPPUNIT_ASSERT(!v);
}

void RequestTest::testConnectedAddrConfirmation()
{
  Request req;

  req.setConnectedAddrInfo("example.org", "192.0.2.1", 443);
  CPPUNIT_ASSERT(!req.connectedAddrInfoConfirmed());

  req.confirmConnectedAddrInfo();
  CPPUNIT_ASSERT(req.connectedAddrInfoConfirmed());

  req.unconfirmConnectedAddrInfo();
  CPPUNIT_ASSERT(!req.connectedAddrInfoConfirmed());

  req.confirmConnectedAddrInfo();
  req.setConnectedAddrInfo("example.org", "2001:db8::1", 443);
  CPPUNIT_ASSERT(!req.connectedAddrInfoConfirmed());
}

void RequestTest::testResetConnectedAddrInfo()
{
  Request req;

  req.setConnectedAddrInfo("example.org", "192.0.2.1", 443);
  req.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                         "svc.example", 8443, "http/1.1");
  CPPUNIT_ASSERT(req.hasHttpsServiceBindingEndpointInfo());
  req.confirmConnectedAddrInfo();
  req.resetConnectedAddrInfo();

  CPPUNIT_ASSERT_EQUAL(std::string(), req.getConnectedHostname());
  CPPUNIT_ASSERT_EQUAL(std::string(), req.getConnectedAddr());
  CPPUNIT_ASSERT_EQUAL((uint16_t)0, req.getConnectedPort());
  CPPUNIT_ASSERT(!req.connectedAddrInfoConfirmed());
  CPPUNIT_ASSERT(!req.hasHttpsServiceBindingEndpointInfo());
}

void RequestTest::testRedirectUri()
{
  Request req;
  req.supportsPersistentConnection(false);
  req.setUri("http://aria.rednoah.com:8080/aria2/index.html");

  // See port number is preserved.
  CPPUNIT_ASSERT(req.redirectUri("/foo"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com:8080/foo"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(1, req.getRedirectCount());

  CPPUNIT_ASSERT(req.redirectUri("http://aria.rednoah.co.jp/"));
  // persistent connection flag is set to be true after redirection
  CPPUNIT_ASSERT(req.supportsPersistentConnection());
  // uri must be the same
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com:8080/aria2/"
                                   "index.html"),
                       req.getUri());
  // currentUri must be updated
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.co.jp/"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(std::string("http"), req.getProtocol());
  CPPUNIT_ASSERT_EQUAL(std::string("aria.rednoah.co.jp"), req.getHost());
  CPPUNIT_ASSERT_EQUAL((uint16_t)80, req.getPort());
  CPPUNIT_ASSERT_EQUAL(std::string("/"), req.getDir());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getFile());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getQuery());
  // See redirect count is incremented.
  CPPUNIT_ASSERT_EQUAL(2, req.getRedirectCount());

  // Give absolute path
  CPPUNIT_ASSERT(req.redirectUri("/abspath/to/file"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.co.jp/abspath/to/file"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(3, req.getRedirectCount());

  // Give relative path
  CPPUNIT_ASSERT(req.redirectUri("relativepath/to/file"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.co.jp/abspath/to/"
                                   "relativepath/to/file"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(4, req.getRedirectCount());

  // Give network-path reference
  CPPUNIT_ASSERT(req.redirectUri("//host/to/file"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/to/file"), req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(5, req.getRedirectCount());

  // http:// in query part
  CPPUNIT_ASSERT(req.redirectUri("/abspath?uri=http://foo"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/abspath?uri=http://foo"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(6, req.getRedirectCount());
}

void RequestTest::testRedirectUri2()
{
  Request req;
  req.setUri("http://aria.rednoah.com/download.html");
  req.setReferer("http://aria.rednoah.com/");
  req.redirectUri("http://aria.rednoah.com/403.html");

  // referer must not be changed in redirection
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/"),
                       req.getReferer());
}

void RequestTest::testResetUri()
{
  Request req;
  req.setUri("http://aria.rednoah.com:8080/aria2/index.html");
  req.setReferer("http://aria.rednoah.com:8080/");
  req.redirectUri("ftp://aria.rednoah.co.jp/index_en.html?view=wide");

  bool v3 = req.resetUri();
  CPPUNIT_ASSERT(v3);
  // currentUri must equal to uri
  CPPUNIT_ASSERT_EQUAL(
      std::string("http://aria.rednoah.com:8080/aria2/index.html"),
      req.getUri());
  CPPUNIT_ASSERT_EQUAL(req.getUri(), req.getCurrentUri());
  // referer is unchanged
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com:8080/"),
                       req.getReferer());
  CPPUNIT_ASSERT_EQUAL(std::string("http"), req.getProtocol());
  CPPUNIT_ASSERT_EQUAL((uint16_t)8080, req.getPort());
  CPPUNIT_ASSERT_EQUAL(std::string("aria.rednoah.com"), req.getHost());
  CPPUNIT_ASSERT_EQUAL(std::string("/aria2/"), req.getDir());
  CPPUNIT_ASSERT_EQUAL(std::string("index.html"), req.getFile());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getQuery());
}

void RequestTest::testInnerLink()
{
  Request req;
  bool v = req.setUri("http://aria.rednoah.com/index.html#download");
  CPPUNIT_ASSERT(v);
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/index.html"
                                   "#download"),
                       req.getUri());
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/index.html"),
                       req.getCurrentUri());
  CPPUNIT_ASSERT_EQUAL(std::string("index.html"), req.getFile());
  CPPUNIT_ASSERT_EQUAL(std::string(""), req.getQuery());
}

void RequestTest::testInnerLinkInReferer()
{
  Request req;
  req.setReferer("http://aria.rednoah.com/home.html#top");
  CPPUNIT_ASSERT_EQUAL(std::string("http://aria.rednoah.com/home.html"),
                       req.getReferer());
}

void RequestTest::testSetUri_supportsPersistentConnection()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("http://host/file"));
  req.supportsPersistentConnection(false);
  CPPUNIT_ASSERT(!req.supportsPersistentConnection());
  req.setUri("http://host/file");
  CPPUNIT_ASSERT(req.supportsPersistentConnection());
}

void RequestTest::testResetUri_supportsPersistentConnection()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("http://host/file"));
  req.supportsPersistentConnection(false);
  CPPUNIT_ASSERT(!req.supportsPersistentConnection());
  req.resetUri();
  CPPUNIT_ASSERT(req.supportsPersistentConnection());
}

void RequestTest::testRedirectUri_supportsPersistentConnection()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("http://host/file"));
  req.supportsPersistentConnection(false);
  CPPUNIT_ASSERT(!req.supportsPersistentConnection());
  req.redirectUri("http://host/file");
  CPPUNIT_ASSERT(req.supportsPersistentConnection());
}

void RequestTest::testHttp2OriginCoalescingState()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("https://example.org/file"));
  req.setHttp2OriginCoalesced(true);
  CPPUNIT_ASSERT(req.isHttp2OriginCoalesced());

  req.blockHttp2OriginCoalescing();
  CPPUNIT_ASSERT(!req.isHttp2OriginCoalesced());
  CPPUNIT_ASSERT(req.http2OriginCoalescingBlocked());

  req.setConnectedAddrInfo("example.org", "192.0.2.1", 443);
  req.setHttp2OriginCoalesced(true);
  CPPUNIT_ASSERT(req.isHttp2OriginCoalesced());
  req.setConnectedAddrInfo("example.org", "192.0.2.2", 443);
  CPPUNIT_ASSERT(!req.isHttp2OriginCoalesced());
  CPPUNIT_ASSERT(req.http2OriginCoalescingBlocked());

  CPPUNIT_ASSERT(req.redirectUri("https://cdn.example/file"));
  CPPUNIT_ASSERT(!req.isHttp2OriginCoalesced());
  CPPUNIT_ASSERT(!req.http2OriginCoalescingBlocked());
}

void RequestTest::testHttpsServiceBindingEndpointInfo()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("https://origin.example/file"));

  req.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                         "svc.example", 8443, "http/1.1");
  CPPUNIT_ASSERT(req.hasHttpsServiceBindingEndpointInfo());
  const auto& info = req.getHttpsServiceBindingEndpointInfo();
  CPPUNIT_ASSERT_EQUAL(std::string("origin.example"), info.originHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, info.originPort);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example"), info.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, info.connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), info.alpn);
  CPPUNIT_ASSERT(info.serviceBindingUsed());

  CPPUNIT_ASSERT(req.redirectUri("https://redirect.example/file"));
  CPPUNIT_ASSERT(!req.hasHttpsServiceBindingEndpointInfo());

  req.setHttpsServiceBindingEndpointInfo("origin.example", 443,
                                         "origin.example", 443, "h2");
  CPPUNIT_ASSERT(!req.getHttpsServiceBindingEndpointInfo()
                      .serviceBindingUsed());
  req.clearHttpsServiceBindingEndpointInfo();
  CPPUNIT_ASSERT(!req.hasHttpsServiceBindingEndpointInfo());
}

void RequestTest::testRedirectUri_uriNormalization()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("http://host/file?a"));

  CPPUNIT_ASSERT(req.redirectUri("/redir1"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/redir1"), req.getCurrentUri());

  CPPUNIT_ASSERT(req.redirectUri("/redir2?b"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/redir2?b"),
                       req.getCurrentUri());

  CPPUNIT_ASSERT(req.redirectUri("/redir3?c#d"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/redir3?c"),
                       req.getCurrentUri());

  CPPUNIT_ASSERT(req.redirectUri("/redir4/gone/.././2nd/foo?a"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/redir4/2nd/foo?a"),
                       req.getCurrentUri());

  CPPUNIT_ASSERT(req.redirectUri("../new2nd/bar?b"));
  CPPUNIT_ASSERT_EQUAL(std::string("http://host/redir4/new2nd/bar?b"),
                       req.getCurrentUri());
}

void RequestTest::testGetURIHost()
{
  Request req;
  CPPUNIT_ASSERT(req.setUri("http://[::1]"));
  CPPUNIT_ASSERT_EQUAL(std::string("[::1]"), req.getURIHost());
}

} // namespace aria2
