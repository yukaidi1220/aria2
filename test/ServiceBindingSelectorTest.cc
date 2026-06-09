#include "ServiceBindingSelector.h"

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class ServiceBindingSelectorTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(ServiceBindingSelectorTest);
  CPPUNIT_TEST(testSelectSortsByPriorityAndAlpnPreference);
  CPPUNIT_TEST(testSelectUsesDefaultAlpn);
  CPPUNIT_TEST(testSelectRejectsAliasModeRecord);
  CPPUNIT_TEST(testSelectRejectsUnavailableRecords);
  CPPUNIT_TEST(testSelectHonorsMandatoryEch);
  CPPUNIT_TEST(testSelectCarriesOptionalEchWhenEnabled);
  CPPUNIT_TEST(testSelectRequiresSupportedDefaultAlpn);
  CPPUNIT_TEST(testSelectRejectsZeroPort);
  CPPUNIT_TEST(testSelectRequiresExplicitAlpnWhenDefaultDisabled);
  CPPUNIT_TEST(testGetAddressHintsByFamily);
  CPPUNIT_TEST(testSelectEndpointsPreservesOriginAndConnectTarget);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSelectSortsByPriorityAndAlpnPreference();
  void testSelectUsesDefaultAlpn();
  void testSelectRejectsAliasModeRecord();
  void testSelectRejectsUnavailableRecords();
  void testSelectHonorsMandatoryEch();
  void testSelectCarriesOptionalEchWhenEnabled();
  void testSelectRequiresSupportedDefaultAlpn();
  void testSelectRejectsZeroPort();
  void testSelectRequiresExplicitAlpnWhenDefaultDisabled();
  void testGetAddressHintsByFamily();
  void testSelectEndpointsPreservesOriginAndConnectTarget();
};

CPPUNIT_TEST_SUITE_REGISTRATION(ServiceBindingSelectorTest);

namespace {

dns::ServiceBindingRecord createRecord(uint16_t priority,
                                       const std::string& targetName)
{
  dns::ServiceBindingRecord record;
  record.ownerName = "www.example.com";
  record.priority = priority;
  record.targetName = targetName;
  return record;
}

dns::ServiceBindingSelectionConfig createConfig()
{
  dns::ServiceBindingSelectionConfig config;
  config.defaultAlpn = "http/1.1";
  config.supportedAlpns.push_back("h2");
  config.supportedAlpns.push_back("http/1.1");
  config.defaultPort = 443;
  return config;
}

} // namespace

void ServiceBindingSelectorTest::testSelectSortsByPriorityAndAlpnPreference()
{
  auto high = createRecord(3, "high.example.com");
  high.alpn.push_back("http/1.1");

  auto low = createRecord(1, "low.example.com");
  low.alpn.push_back("h3");
  low.alpn.push_back("h2");
  low.hasPort = true;
  low.port = 8443;

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(high);
  records.push_back(low);

  auto result = dns::selectServiceBindings(records, createConfig());

  CPPUNIT_ASSERT_EQUAL((size_t)2, result.size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)1, result[0].priority);
  CPPUNIT_ASSERT_EQUAL(std::string("low.example.com"), result[0].targetName);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, result[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), result[0].alpn);
  CPPUNIT_ASSERT(result[0].echConfigList.empty());
  CPPUNIT_ASSERT(!result[0].defaultAlpnUsed);

  CPPUNIT_ASSERT_EQUAL(std::string("high.example.com"), result[1].targetName);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, result[1].port);
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), result[1].alpn);
}

void ServiceBindingSelectorTest::testSelectUsesDefaultAlpn()
{
  auto record = createRecord(1, std::string());

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(record);

  auto result = dns::selectServiceBindings(records, createConfig());

  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"), result[0].targetName);
  CPPUNIT_ASSERT_EQUAL(std::string("http/1.1"), result[0].alpn);
  CPPUNIT_ASSERT(result[0].defaultAlpnUsed);
}

void ServiceBindingSelectorTest::testSelectRejectsAliasModeRecord()
{
  auto alias = createRecord(0, "alias.example.com");
  alias.alpn.push_back("h2");
  alias.hasPort = true;
  alias.port = 8443;
  alias.ipv4hint.push_back("192.0.2.1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(alias);

  auto result = dns::selectServiceBindings(records, createConfig());

  CPPUNIT_ASSERT(result.empty());
}

void ServiceBindingSelectorTest::testSelectRejectsUnavailableRecords()
{
  auto alias = createRecord(0, "alias.example.com");

  auto aliasUnavailable = createRecord(0, std::string());
  aliasUnavailable.aliasModeUnavailable = true;

  auto unknownMandatory = createRecord(1, "mandatory.example.com");
  unknownMandatory.hasUnknownMandatoryKey = true;

  auto h3Only = createRecord(2, "h3.example.com");
  h3Only.noDefaultAlpn = true;
  h3Only.alpn.push_back("h3");

  auto h2 = createRecord(3, "h2.example.com");
  h2.alpn.push_back("h2");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(alias);
  records.push_back(aliasUnavailable);
  records.push_back(unknownMandatory);
  records.push_back(h3Only);
  records.push_back(h2);

  auto result = dns::selectServiceBindings(records, createConfig());

  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("h2.example.com"), result[0].targetName);
}

void ServiceBindingSelectorTest::testSelectHonorsMandatoryEch()
{
  auto ech = createRecord(1, "ech.example.com");
  ech.alpn.push_back("h2");
  ech.mandatoryKeys.push_back(5);
  ech.paramKeys.push_back(1);
  ech.paramKeys.push_back(5);
  ech.echConfigList = "ech-config";

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(ech);

  auto config = createConfig();
  auto disabled = dns::selectServiceBindings(records, config);
  CPPUNIT_ASSERT(disabled.empty());

  config.echConfigListEnabled = true;
  auto enabled = dns::selectServiceBindings(records, config);
  CPPUNIT_ASSERT_EQUAL((size_t)1, enabled.size());
  CPPUNIT_ASSERT_EQUAL(std::string("ech-config"), enabled[0].echConfigList);
}

void ServiceBindingSelectorTest::testSelectCarriesOptionalEchWhenEnabled()
{
  auto ech = createRecord(1, "optional-ech.example.com");
  ech.alpn.push_back("h2");
  ech.echConfigList = "optional-ech-config";

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(ech);

  auto config = createConfig();
  auto disabled = dns::selectServiceBindings(records, config);
  CPPUNIT_ASSERT_EQUAL((size_t)1, disabled.size());
  CPPUNIT_ASSERT(disabled[0].echConfigList.empty());

  config.echConfigListEnabled = true;
  auto enabled = dns::selectServiceBindings(records, config);
  CPPUNIT_ASSERT_EQUAL((size_t)1, enabled.size());
  CPPUNIT_ASSERT_EQUAL(std::string("optional-ech-config"),
                       enabled[0].echConfigList);
}

void ServiceBindingSelectorTest::testSelectRequiresSupportedDefaultAlpn()
{
  auto record = createRecord(1, "default.example.com");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(record);

  auto config = createConfig();
  config.supportedAlpns.clear();
  auto result = dns::selectServiceBindings(records, config);

  CPPUNIT_ASSERT(result.empty());
}

void ServiceBindingSelectorTest::testSelectRejectsZeroPort()
{
  auto implicitZero = createRecord(1, "implicit-zero.example.com");
  implicitZero.alpn.push_back("h2");

  auto explicitZero = createRecord(2, "explicit-zero.example.com");
  explicitZero.alpn.push_back("h2");
  explicitZero.hasPort = true;
  explicitZero.port = 0;

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(implicitZero);
  records.push_back(explicitZero);

  auto config = createConfig();
  config.defaultPort = 0;
  auto result = dns::selectServiceBindings(records, config);

  CPPUNIT_ASSERT(result.empty());
}

void ServiceBindingSelectorTest::testSelectRequiresExplicitAlpnWhenDefaultDisabled()
{
  auto noMatch = createRecord(1, "no-match.example.com");
  noMatch.noDefaultAlpn = true;
  noMatch.alpn.push_back("h3");

  auto match = createRecord(2, "match.example.com");
  match.noDefaultAlpn = true;
  match.alpn.push_back("h2");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(noMatch);
  records.push_back(match);

  auto result = dns::selectServiceBindings(records, createConfig());

  CPPUNIT_ASSERT_EQUAL((size_t)1, result.size());
  CPPUNIT_ASSERT_EQUAL(std::string("match.example.com"), result[0].targetName);
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), result[0].alpn);
  CPPUNIT_ASSERT(!result[0].defaultAlpnUsed);
}

void ServiceBindingSelectorTest::testGetAddressHintsByFamily()
{
  auto record = createRecord(1, "svc.example.com");
  record.ipv4hint.push_back("192.0.2.1");
  record.ipv4hint.push_back("198.51.100.2");
  record.ipv6hint.push_back("2001:db8::1");

  auto ipv4 = dns::getServiceBindingAddressHints(
      record, dns::SVCB_ADDRESS_FAMILY_IPV4);
  CPPUNIT_ASSERT_EQUAL((size_t)2, ipv4.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), ipv4[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.2"), ipv4[1]);

  auto ipv6 = dns::getServiceBindingAddressHints(
      record, dns::SVCB_ADDRESS_FAMILY_IPV6);
  CPPUNIT_ASSERT_EQUAL((size_t)1, ipv6.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), ipv6[0]);

  auto all = dns::getServiceBindingAddressHints(
      record, dns::SVCB_ADDRESS_FAMILY_UNSPEC);
  CPPUNIT_ASSERT_EQUAL((size_t)3, all.size());
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), all[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), all[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("198.51.100.2"), all[2]);
}

void ServiceBindingSelectorTest::
    testSelectEndpointsPreservesOriginAndConnectTarget()
{
  auto alternate = createRecord(1, "svc.example.com");
  alternate.alpn.push_back("h2");
  alternate.hasPort = true;
  alternate.port = 8443;
  alternate.ipv4hint.push_back("192.0.2.1");
  alternate.echConfigList = "optional-ech";

  auto origin = createRecord(2, std::string());
  origin.alpn.push_back("http/1.1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(alternate);
  records.push_back(origin);

  auto config = createConfig();
  config.echConfigListEnabled = true;
  auto endpoints = dns::selectServiceBindingEndpoints(
      records, "www.example.com", 443, config);

  CPPUNIT_ASSERT_EQUAL((size_t)2, endpoints.size());
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"),
                       endpoints[0].originHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, endpoints[0].originPort);
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.com"),
                       endpoints[0].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, endpoints[0].connectPort);
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), endpoints[0].alpn);
  CPPUNIT_ASSERT_EQUAL(std::string("optional-ech"),
                       endpoints[0].echConfigList);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       endpoints[0].addressHints[0]);
  CPPUNIT_ASSERT(endpoints[0].serviceBindingUsed());

  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"),
                       endpoints[1].originHost);
  CPPUNIT_ASSERT_EQUAL(std::string("www.example.com"),
                       endpoints[1].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, endpoints[1].connectPort);
  CPPUNIT_ASSERT(!endpoints[1].serviceBindingUsed());
}

} // namespace aria2
