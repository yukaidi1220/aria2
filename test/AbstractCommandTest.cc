#include "AbstractCommand.h"

#include <iostream>
#include <cppunit/extensions/HelperMacros.h>

#include "Option.h"
#include "prefs.h"
#include "SocketCore.h"
#include "SocketRecvBuffer.h"
#include "FileEntry.h"
#include "InorderURISelector.h"

namespace aria2 {

class AbstractCommandTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AbstractCommandTest);
  CPPUNIT_TEST(testGetProxyUri);
  CPPUNIT_TEST(testSelectIPAddressReturnsEmptyForEmptyList);
  CPPUNIT_TEST(testSelectIPAddressKeepsSingleFamilyOrder);
  CPPUNIT_TEST(testSelectIPAddressAlternatesDualStackByCuid);
  CPPUNIT_TEST(testSelectIPAddressPrefersRequestedDualStackFamily);
  CPPUNIT_TEST(testSelectIPAddressPrefersUnderusedActiveFamily);
  CPPUNIT_TEST(testPrioritizeIPAddress);
  CPPUNIT_TEST(testPrioritizeIPAddressIgnoresUnknownAddress);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}

  void tearDown() {}

  void testGetProxyUri();
  void testSelectIPAddressReturnsEmptyForEmptyList();
  void testSelectIPAddressKeepsSingleFamilyOrder();
  void testSelectIPAddressAlternatesDualStackByCuid();
  void testSelectIPAddressPrefersRequestedDualStackFamily();
  void testSelectIPAddressPrefersUnderusedActiveFamily();
  void testPrioritizeIPAddress();
  void testPrioritizeIPAddressIgnoresUnknownAddress();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AbstractCommandTest);

void AbstractCommandTest::testGetProxyUri()
{
  Option op;
  CPPUNIT_ASSERT_EQUAL(std::string(), getProxyUri("http", &op));

  op.put(PREF_HTTP_PROXY, "http://hu:hp@httpproxy/");
  op.put(PREF_FTP_PROXY, "ftp://fu:fp@ftpproxy/");
  CPPUNIT_ASSERT_EQUAL(std::string("http://hu:hp@httpproxy/"),
                       getProxyUri("http", &op));
  CPPUNIT_ASSERT_EQUAL(std::string("ftp://fu:fp@ftpproxy/"),
                       getProxyUri("ftp", &op));

  op.put(PREF_ALL_PROXY, "http://au:ap@allproxy/");
  CPPUNIT_ASSERT_EQUAL(std::string("http://au:ap@allproxy/"),
                       getProxyUri("https", &op));

  op.put(PREF_ALL_PROXY_USER, "aunew");
  op.put(PREF_ALL_PROXY_PASSWD, "apnew");
  CPPUNIT_ASSERT_EQUAL(std::string("http://aunew:apnew@allproxy/"),
                       getProxyUri("https", &op));

  op.put(PREF_HTTPS_PROXY, "http://hsu:hsp@httpsproxy/");
  CPPUNIT_ASSERT_EQUAL(std::string("http://hsu:hsp@httpsproxy/"),
                       getProxyUri("https", &op));

  CPPUNIT_ASSERT_EQUAL(std::string(), getProxyUri("unknown", &op));

  op.put(PREF_HTTP_PROXY_USER, "hunew");
  CPPUNIT_ASSERT_EQUAL(std::string("http://hunew:hp@httpproxy/"),
                       getProxyUri("http", &op));

  op.put(PREF_HTTP_PROXY_PASSWD, "hpnew");
  CPPUNIT_ASSERT_EQUAL(std::string("http://hunew:hpnew@httpproxy/"),
                       getProxyUri("http", &op));

  op.put(PREF_HTTP_PROXY_USER, "");
  CPPUNIT_ASSERT_EQUAL(std::string("http://httpproxy/"),
                       getProxyUri("http", &op));
}

void AbstractCommandTest::testSelectIPAddressReturnsEmptyForEmptyList()
{
  std::vector<std::string> addrs;

  CPPUNIT_ASSERT_EQUAL(std::string(), selectIPAddress(addrs, 1));
}

void AbstractCommandTest::testSelectIPAddressKeepsSingleFamilyOrder()
{
  std::vector<std::string> ipv4Addrs;
  ipv4Addrs.push_back("192.0.2.1");
  ipv4Addrs.push_back("192.0.2.2");
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(ipv4Addrs, 2));

  std::vector<std::string> ipv6Addrs;
  ipv6Addrs.push_back("2001:db8::1");
  ipv6Addrs.push_back("2001:db8::2");
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(ipv6Addrs, 2));
}

void AbstractCommandTest::testSelectIPAddressAlternatesDualStackByCuid()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 1));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), selectIPAddress(addrs, 2));
}

void AbstractCommandTest::testSelectIPAddressPrefersRequestedDualStackFamily()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 1, AF_INET));
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, AF_INET6));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, 0));
}

void AbstractCommandTest::testSelectIPAddressPrefersUnderusedActiveFamily()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  auto fileEntry = std::make_shared<FileEntry>();
  fileEntry->setMaxConnectionPerServer(2);
  fileEntry->setUris(std::vector<std::string>{"https://example.org/file",
                                              "https://example.org/file?part=2"});
  InorderURISelector selector{};
  std::vector<std::pair<size_t, std::string>> usedHosts;

  auto req = fileEntry->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req);
  auto req2 = fileEntry->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req2);
  req->setConnectedAddrInfo("example.org", "192.0.2.1", 443);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                        443));

  req->confirmConnectedAddrInfo();
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));

  fileEntry->poolRequest(req);
  CPPUNIT_ASSERT(!req->connectedAddrInfoConfirmed());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                        443));

  req2->setConnectedAddrInfo("example.org", "192.0.2.2", 443);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));

  req2->setConnectedAddrInfo("other.example.org", "192.0.2.1", 443);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));

  req2->setConnectedAddrInfo("example.org", "192.0.2.1", 8443);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));
}

void AbstractCommandTest::testPrioritizeIPAddress()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");
  addrs.push_back("192.0.2.2");

  prioritizeIPAddress(addrs, "192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.2"), addrs[2]);
}

void AbstractCommandTest::testPrioritizeIPAddressIgnoresUnknownAddress()
{
  std::vector<std::string> addrs;
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");

  prioritizeIPAddress(addrs, "192.0.2.2");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[1]);
}

} // namespace aria2
