#include "AbstractCommand.h"

#include <iostream>
#include <cppunit/extensions/HelperMacros.h>

#include "Option.h"
#include "prefs.h"
#include "SocketCore.h"
#include "SocketRecvBuffer.h"
#include "FileEntry.h"
#include "InorderURISelector.h"
#include "DnsMessage.h"

namespace aria2 {

class AbstractCommandTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AbstractCommandTest);
  CPPUNIT_TEST(testGetProxyUri);
  CPPUNIT_TEST(testSelectIPAddressReturnsEmptyForEmptyList);
  CPPUNIT_TEST(testSelectIPAddressKeepsSingleFamilyOrder);
  CPPUNIT_TEST(testSelectIPAddressAlternatesDualStackByCuid);
  CPPUNIT_TEST(testSelectIPAddressPrefersIPv4OverScopedIPv6);
  CPPUNIT_TEST(testSelectIPAddressPrefersGlobalIPv6OverScopedIPv6);
  CPPUNIT_TEST(testSelectIPAddressRotatesDualStackByFileEntry);
  CPPUNIT_TEST(testSelectIPAddressDoesNotRotateToScopedIPv6);
  CPPUNIT_TEST(testSelectIPAddressPrefersRequestedDualStackFamily);
  CPPUNIT_TEST(testSelectIPAddressPrefersUnderusedActiveFamily);
  CPPUNIT_TEST(testSelectIPAddressPrefersHealthyFamily);
  CPPUNIT_TEST(testSelectIPAddressFallsBackWhenBothFamiliesPenalized);
  CPPUNIT_TEST(testSelectIPAddressRotatesWhenActiveFamiliesBalanced);
  CPPUNIT_TEST(testSelectIPAddressCountsActiveFamilyOutsideCandidates);
  CPPUNIT_TEST(testSelectIPAddressIgnoresOtherActiveHostAndPort);
  CPPUNIT_TEST(testPrioritizeIPAddress);
  CPPUNIT_TEST(testPrioritizeIPAddressIgnoresUnknownAddress);
  CPPUNIT_TEST(testPrioritizeAndInterleaveIPAddress);
  CPPUNIT_TEST(testPrioritizeAndInterleaveIPAddressKeepsSingleFamilyOrder);
  CPPUNIT_TEST(testPrioritizeAndInterleaveIPAddressKeepsNonNumericAddress);
  CPPUNIT_TEST(testGetUsableHttpsServiceBindingAddressHints);
  CPPUNIT_TEST(testGetUsableHttpsServiceBindingAddressHintsHonorsAsyncDns);
  CPPUNIT_TEST(testGetUsableHttpsServiceBindingAddressHintsHonorsDisableIPv6);
  CPPUNIT_TEST(testGetUsableHttpsServiceBindingAddressHintsRejectsH2Only);
  CPPUNIT_TEST(testGetHttpsServiceBindingEndpointsKeepsConnectTarget);
#ifdef ENABLE_ASYNC_DNS
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesDisabledByDefault);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesDisabledByAsyncDns);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesCaresSystem);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesCaresExplicit);
#ifdef ENABLE_SSL
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesIgnoresSecureConfigWhenAsyncDnsOff);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesDot);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesDoh);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureFirst);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureOnly);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesMultiPlainOnly);
  CPPUNIT_TEST(testCreateHttpsServiceBindingDiscoveryPhasesMultiSystemOnly);
#endif // ENABLE_SSL
#endif // ENABLE_ASYNC_DNS
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}

  void tearDown() {}

  void testGetProxyUri();
  void testSelectIPAddressReturnsEmptyForEmptyList();
  void testSelectIPAddressKeepsSingleFamilyOrder();
  void testSelectIPAddressAlternatesDualStackByCuid();
  void testSelectIPAddressPrefersIPv4OverScopedIPv6();
  void testSelectIPAddressPrefersGlobalIPv6OverScopedIPv6();
  void testSelectIPAddressRotatesDualStackByFileEntry();
  void testSelectIPAddressDoesNotRotateToScopedIPv6();
  void testSelectIPAddressPrefersRequestedDualStackFamily();
  void testSelectIPAddressPrefersUnderusedActiveFamily();
  void testSelectIPAddressPrefersHealthyFamily();
  void testSelectIPAddressFallsBackWhenBothFamiliesPenalized();
  void testSelectIPAddressRotatesWhenActiveFamiliesBalanced();
  void testSelectIPAddressCountsActiveFamilyOutsideCandidates();
  void testSelectIPAddressIgnoresOtherActiveHostAndPort();
  void testPrioritizeIPAddress();
  void testPrioritizeIPAddressIgnoresUnknownAddress();
  void testPrioritizeAndInterleaveIPAddress();
  void testPrioritizeAndInterleaveIPAddressKeepsSingleFamilyOrder();
  void testPrioritizeAndInterleaveIPAddressKeepsNonNumericAddress();
  void testGetUsableHttpsServiceBindingAddressHints();
  void testGetUsableHttpsServiceBindingAddressHintsHonorsAsyncDns();
  void testGetUsableHttpsServiceBindingAddressHintsHonorsDisableIPv6();
  void testGetUsableHttpsServiceBindingAddressHintsRejectsH2Only();
  void testGetHttpsServiceBindingEndpointsKeepsConnectTarget();
#ifdef ENABLE_ASYNC_DNS
  void testCreateHttpsServiceBindingDiscoveryPhasesDisabledByDefault();
  void testCreateHttpsServiceBindingDiscoveryPhasesDisabledByAsyncDns();
  void testCreateHttpsServiceBindingDiscoveryPhasesCaresSystem();
  void testCreateHttpsServiceBindingDiscoveryPhasesCaresExplicit();
#ifdef ENABLE_SSL
  void
  testCreateHttpsServiceBindingDiscoveryPhasesIgnoresSecureConfigWhenAsyncDnsOff();
  void testCreateHttpsServiceBindingDiscoveryPhasesDot();
  void testCreateHttpsServiceBindingDiscoveryPhasesDoh();
  void testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureFirst();
  void testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureOnly();
  void testCreateHttpsServiceBindingDiscoveryPhasesMultiPlainOnly();
  void testCreateHttpsServiceBindingDiscoveryPhasesMultiSystemOnly();
#endif // ENABLE_SSL
#endif // ENABLE_ASYNC_DNS
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

void AbstractCommandTest::testSelectIPAddressPrefersIPv4OverScopedIPv6()
{
  std::vector<std::string> addrs;
  addrs.push_back("fd00::1");
  addrs.push_back("fe80::1");
  addrs.push_back("fec0::1");
  addrs.push_back("::ffff:192.0.2.1");
  addrs.push_back("::1");
  addrs.push_back("ff02::1");
  addrs.push_back("192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), selectIPAddress(addrs, 1));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), selectIPAddress(addrs, 2));
}

void AbstractCommandTest::testSelectIPAddressPrefersGlobalIPv6OverScopedIPv6()
{
  std::vector<std::string> addrs;
  addrs.push_back("fd00::1");
  addrs.push_back("2001:db8::1");
  addrs.push_back("192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), selectIPAddress(addrs, 1));
}

void AbstractCommandTest::testSelectIPAddressRotatesDualStackByFileEntry()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");
  auto fileEntry = std::make_shared<FileEntry>();

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 1, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 3, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 4, fileEntry, "example.org",
                                       8443));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 5, fileEntry, "mirror.example",
                                       443));
}

void AbstractCommandTest::testSelectIPAddressDoesNotRotateToScopedIPv6()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("fd00::1");
  auto fileEntry = std::make_shared<FileEntry>();

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 1, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));
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
}

void AbstractCommandTest::testSelectIPAddressPrefersHealthyFamily()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  auto fileEntry = std::make_shared<FileEntry>();
  fileEntry->setUris(std::vector<std::string>{"https://example.org/file"});
  InorderURISelector selector{};
  std::vector<std::pair<size_t, std::string>> usedHosts;

  auto req = fileEntry->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req);
  req->setConnectedAddrInfo("example.org", "192.0.2.2", 443);
  fileEntry->recordAddressFamilyFailure("example.org", 443, AF_INET6);

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 1, fileEntry, "example.org",
                                       443));
}

void AbstractCommandTest::testSelectIPAddressFallsBackWhenBothFamiliesPenalized()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  auto fileEntry = std::make_shared<FileEntry>();
  fileEntry->recordAddressFamilyFailure("example.org", 443, AF_INET);
  fileEntry->recordAddressFamilyFailure("example.org", 443, AF_INET6);

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 1, fileEntry, "example.org",
                                       443));
}

void AbstractCommandTest::testSelectIPAddressRotatesWhenActiveFamiliesBalanced()
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
  req->setConnectedAddrInfo("example.org", "192.0.2.55", 443);
  req2->setConnectedAddrInfo("example.org", "2001:db8::55", 443);

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 1, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 3, fileEntry, "example.org",
                                       443));
}

void AbstractCommandTest::testSelectIPAddressCountsActiveFamilyOutsideCandidates()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  auto fileEntry = std::make_shared<FileEntry>();
  fileEntry->setUris(std::vector<std::string>{"https://example.org/file"});
  InorderURISelector selector{};
  std::vector<std::pair<size_t, std::string>> usedHosts;

  auto req = fileEntry->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req);
  req->setConnectedAddrInfo("example.org", "192.0.2.2", 443);

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"),
                       selectIPAddress(addrs, 2, fileEntry, "example.org",
                                       443));
}

void AbstractCommandTest::testSelectIPAddressIgnoresOtherActiveHostAndPort()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("2001:db8::1");

  auto otherHost = std::make_shared<FileEntry>();
  otherHost->setUris(std::vector<std::string>{"https://example.org/file"});
  InorderURISelector selector{};
  std::vector<std::pair<size_t, std::string>> usedHosts;

  auto req = otherHost->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req);
  req->setConnectedAddrInfo("other.example.org", "192.0.2.1", 443);

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, otherHost, "example.org",
                                       443));

  auto otherPort = std::make_shared<FileEntry>();
  otherPort->setUris(std::vector<std::string>{"https://example.org/file"});
  req = otherPort->getRequest(&selector, true, usedHosts);
  CPPUNIT_ASSERT(req);
  req->setConnectedAddrInfo("example.org", "192.0.2.1", 8443);

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       selectIPAddress(addrs, 2, otherPort, "example.org",
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

void AbstractCommandTest::testPrioritizeAndInterleaveIPAddress()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("192.0.2.2");
  addrs.push_back("2001:db8::1");
  addrs.push_back("2001:db8::2");
  addrs.push_back("192.0.2.3");

  prioritizeAndInterleaveIPAddress(addrs, "192.0.2.2");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.2"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[2]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::2"), addrs[3]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.3"), addrs[4]);

  prioritizeAndInterleaveIPAddress(addrs, "2001:db8::1");

  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.2"), addrs[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::2"), addrs[2]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[3]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.3"), addrs[4]);
}

void AbstractCommandTest::
    testPrioritizeAndInterleaveIPAddressKeepsSingleFamilyOrder()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("192.0.2.2");
  addrs.push_back("192.0.2.3");

  prioritizeAndInterleaveIPAddress(addrs, "192.0.2.2");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.2"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.3"), addrs[2]);
}

void AbstractCommandTest::
    testPrioritizeAndInterleaveIPAddressKeepsNonNumericAddress()
{
  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  addrs.push_back("target.example");
  addrs.push_back("2001:db8::1");

  prioritizeAndInterleaveIPAddress(addrs, "192.0.2.1");

  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), addrs[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("target.example"), addrs[2]);
}

namespace {
dns::ServiceBindingRecord createHttpsServiceBindingRecord(
    uint16_t priority, const std::string& ownerName,
    const std::string& targetName)
{
  dns::ServiceBindingRecord record;
  record.ownerName = ownerName;
  record.priority = priority;
  record.targetName = targetName;
  record.alpn.push_back("http/1.1");
  return record;
}

#ifdef ENABLE_ASYNC_DNS
void assertHttpsServiceBindingDiscoveryPhases(
    const std::vector<HttpsServiceBindingDiscoveryPhase>& expected,
    const Option& option)
{
  auto phases = createHttpsServiceBindingDiscoveryPhases(&option);
  CPPUNIT_ASSERT_EQUAL(expected.size(), phases.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    CPPUNIT_ASSERT_EQUAL(expected[i], phases[i]);
  }
}
#endif // ENABLE_ASYNC_DNS
} // namespace

void AbstractCommandTest::testGetUsableHttpsServiceBindingAddressHints()
{
  auto usable = createHttpsServiceBindingRecord(
      1, "example.org", std::string());
  usable.ipv4hint.push_back("192.0.2.1");
  usable.ipv6hint.push_back("2001:db8::1");

  auto differentTarget = createHttpsServiceBindingRecord(
      2, "example.org", "svc.example.org");
  differentTarget.ipv4hint.push_back("192.0.2.2");

  auto differentPort = createHttpsServiceBindingRecord(
      3, "example.org", std::string());
  differentPort.hasPort = true;
  differentPort.port = 8443;
  differentPort.ipv4hint.push_back("192.0.2.3");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(differentPort);
  records.push_back(usable);
  records.push_back(differentTarget);

  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto hints =
      getUsableHttpsServiceBindingAddressHints(records, "example.org", 443,
                                               &option);

  CPPUNIT_ASSERT_EQUAL((size_t)2, hints.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), hints[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), hints[1]);
}

void AbstractCommandTest::
    testGetUsableHttpsServiceBindingAddressHintsHonorsAsyncDns()
{
  auto usable = createHttpsServiceBindingRecord(
      1, "example.org", std::string());
  usable.ipv4hint.push_back("192.0.2.1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(usable);

  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_FALSE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto hints =
      getUsableHttpsServiceBindingAddressHints(records, "example.org", 443,
                                               &option);

  CPPUNIT_ASSERT(hints.empty());
}

void AbstractCommandTest::
    testGetUsableHttpsServiceBindingAddressHintsHonorsDisableIPv6()
{
  auto usable = createHttpsServiceBindingRecord(
      1, "example.org", std::string());
  usable.ipv4hint.push_back("192.0.2.1");
  usable.ipv6hint.push_back("2001:db8::1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(usable);

  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_DISABLE_IPV6, A2_V_TRUE);
  auto hints =
      getUsableHttpsServiceBindingAddressHints(records, "example.org", 443,
                                               &option);

  CPPUNIT_ASSERT_EQUAL((size_t)1, hints.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), hints[0]);
}

void AbstractCommandTest::
    testGetUsableHttpsServiceBindingAddressHintsRejectsH2Only()
{
  auto h2Only = createHttpsServiceBindingRecord(
      1, "example.org", std::string());
  h2Only.alpn.clear();
  h2Only.alpn.push_back("h2");
  h2Only.noDefaultAlpn = true;
  h2Only.ipv4hint.push_back("192.0.2.1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(h2Only);

  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto hints =
      getUsableHttpsServiceBindingAddressHints(records, "example.org", 443,
                                               &option);

  CPPUNIT_ASSERT(hints.empty());
}

void AbstractCommandTest::testGetHttpsServiceBindingEndpointsKeepsConnectTarget()
{
  auto alternate = createHttpsServiceBindingRecord(
      1, "example.org", "svc.example.org");
  alternate.hasPort = true;
  alternate.port = 8443;
  alternate.ipv4hint.push_back("192.0.2.2");

  auto origin = createHttpsServiceBindingRecord(
      2, "example.org", std::string());
  origin.ipv4hint.push_back("192.0.2.1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(alternate);
  records.push_back(origin);

  Option option;
  auto endpoints =
      getHttpsServiceBindingEndpoints(records, "example.org", 443, &option);

  CPPUNIT_ASSERT_EQUAL((size_t)2, endpoints.size());
  CPPUNIT_ASSERT_EQUAL(std::string("example.org"), endpoints[0].originHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, endpoints[0].originPort);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.org"),
                       endpoints[0].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, endpoints[0].connectPort);
  CPPUNIT_ASSERT(endpoints[0].serviceBindingUsed());

  CPPUNIT_ASSERT_EQUAL(std::string("example.org"), endpoints[1].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, endpoints[1].connectPort);
  CPPUNIT_ASSERT(!endpoints[1].serviceBindingUsed());

  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  auto hints =
      getUsableHttpsServiceBindingAddressHints(records, "example.org", 443,
                                               &option);
  CPPUNIT_ASSERT_EQUAL((size_t)1, hints.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), hints[0]);
}

#ifdef ENABLE_ASYNC_DNS
void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesDisabledByDefault()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_CARES);
  option.put(PREF_ASYNC_DNS_SERVER, "");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{}, option);
}

void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesDisabledByAsyncDns()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_FALSE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_CARES);
  option.put(PREF_ASYNC_DNS_SERVER, "1.1.1.1");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{}, option);
}

void AbstractCommandTest::testCreateHttpsServiceBindingDiscoveryPhasesCaresSystem()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_CARES);
  option.put(PREF_ASYNC_DNS_SERVER, "");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::testCreateHttpsServiceBindingDiscoveryPhasesCaresExplicit()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_CARES);
  option.put(PREF_ASYNC_DNS_SERVER, "1.1.1.1");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_EXPLICIT_CARES,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

#ifdef ENABLE_SSL
void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesIgnoresSecureConfigWhenAsyncDnsOff()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_FALSE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{}, option);
}

void AbstractCommandTest::testCreateHttpsServiceBindingDiscoveryPhasesDot()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOT);
  option.put(PREF_ASYNC_DNS_SERVER, "dns.example.org");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SECURE,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::testCreateHttpsServiceBindingDiscoveryPhasesDoh()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_DOH);
  option.put(PREF_ASYNC_DNS_SERVER, "https://dns.example.org/dns-query");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SECURE,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureFirst()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER,
             "udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,"
             "https://dns.example.org/dns-query");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SECURE,
          HTTPS_SERVICE_BINDING_DISCOVERY_EXPLICIT_PLAIN,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureOnly()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER,
             "dot://dns.example.org,https://dns.example.org/dns-query");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SECURE,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesMultiPlainOnly()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER, "udp://1.1.1.1,tcp://1.0.0.1");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_EXPLICIT_PLAIN,
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}

void AbstractCommandTest::
    testCreateHttpsServiceBindingDiscoveryPhasesMultiSystemOnly()
{
  Option option;
  option.put(PREF_ASYNC_DNS, A2_V_TRUE);
  option.put(PREF_ENABLE_HTTPS_RR, A2_V_TRUE);
  option.put(PREF_ASYNC_DNS_MODE, V_MULTI);
  option.put(PREF_ASYNC_DNS_SERVER, "");

  assertHttpsServiceBindingDiscoveryPhases(
      std::vector<HttpsServiceBindingDiscoveryPhase>{
          HTTPS_SERVICE_BINDING_DISCOVERY_SYSTEM_CARES},
      option);
}
#endif // ENABLE_SSL
#endif // ENABLE_ASYNC_DNS

} // namespace aria2
