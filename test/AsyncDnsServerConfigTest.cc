#include "AsyncDnsServerConfig.h"

#include <cppunit/extensions/HelperMacros.h>

#include "Exception.h"

namespace aria2 {

class AsyncDnsServerConfigTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AsyncDnsServerConfigTest);
  CPPUNIT_TEST(testParseDotServerConfig);
  CPPUNIT_TEST(testParseDotServerConfigList);
  CPPUNIT_TEST(testParseDotServerConfig_badInput);
  CPPUNIT_TEST(testValidateDotServerConfigForDirectConnect);
  CPPUNIT_TEST(testParseDohServerConfig);
  CPPUNIT_TEST(testParseDohServerConfigList);
  CPPUNIT_TEST(testParseDohServerConfig_badInput);
  CPPUNIT_TEST(testValidateDohServerConfigForDirectConnect);
  CPPUNIT_TEST_SUITE_END();

public:
  void testParseDotServerConfig();
  void testParseDotServerConfigList();
  void testParseDotServerConfig_badInput();
  void testValidateDotServerConfigForDirectConnect();
  void testParseDohServerConfig();
  void testParseDohServerConfigList();
  void testParseDohServerConfig_badInput();
  void testValidateDohServerConfigForDirectConnect();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncDnsServerConfigTest);

void AsyncDnsServerConfigTest::testParseDotServerConfig()
{
  auto config = parseAsyncDnsDotServerConfig("dns.example.org");
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.tlsHost);

  config = parseAsyncDnsDotServerConfig("dns.example.org:8853");
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8853, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.tlsHost);

  config = parseAsyncDnsDotServerConfig("1.1.1.1");
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);

  config = parseAsyncDnsDotServerConfig("[2606:4700:4700::1111]:853");
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);

  config = parseAsyncDnsDotServerConfig(" [2606:4700:4700::1111] ");
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
}

void AsyncDnsServerConfigTest::testParseDotServerConfigList()
{
  auto configs =
      parseAsyncDnsDotServerConfigList("dns.example.org,1.1.1.1:8853");
  CPPUNIT_ASSERT_EQUAL((size_t)2, configs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), configs[0].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)853, configs[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), configs[0].tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), configs[1].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8853, configs[1].port);
  CPPUNIT_ASSERT_EQUAL(std::string(), configs[1].tlsHost);
}

void AsyncDnsServerConfigTest::testParseDotServerConfig_badInput()
{
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig(""), Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("host:"), Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("host:0"), Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("host:65536"), Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("2606:4700::1111"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("[2606:4700::1111"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfig("[2606:4700::1111]x:853"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDotServerConfigList("dns.example.org,"),
                       Exception);
}

void AsyncDnsServerConfigTest::testValidateDotServerConfigForDirectConnect()
{
  auto configs =
      parseAsyncDnsDotServerConfigList("1.1.1.1,[2606:4700:4700::1111]:853");
  validateAsyncDnsDotServerConfigForDirectConnect(configs);

  configs = parseAsyncDnsDotServerConfigList("dns.example.org");
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), configs[0].connectHost);
  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDotServerConfigForDirectConnect(configs), Exception);

  configs = parseAsyncDnsDotServerConfigList("1.1.1.1,dns.example.org");
  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDotServerConfigForDirectConnect(configs), Exception);

  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDotServerConfigForDirectConnect(
          std::vector<AsyncDnsServerConfig>()),
      Exception);
}

void AsyncDnsServerConfigTest::testParseDohServerConfig()
{
  auto config = parseAsyncDnsDohServerConfig("https://1.1.1.1/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), config.path);

  config = parseAsyncDnsDohServerConfig("https://1.1.1.1:8443/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), config.path);

  config = parseAsyncDnsDohServerConfig("https://1.1.1.1:443/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), config.path);

  config =
      parseAsyncDnsDohServerConfig("https://[2606:4700:4700::1111]/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), config.path);

  config = parseAsyncDnsDohServerConfig(
      " https://[2606:4700:4700::1111]:8443/dns-query?ct=application/dns-message ");
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string(), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query?ct=application/dns-message"),
                       config.path);

  config = parseAsyncDnsDohServerConfig("https://dns.example.org/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, config.port);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), config.tlsHost);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), config.path);
}

void AsyncDnsServerConfigTest::testParseDohServerConfigList()
{
  auto configs = parseAsyncDnsDohServerConfigList(
      "https://1.1.1.1/dns-query,https://[2606:4700:4700::1111]:8443/dns-query");
  CPPUNIT_ASSERT_EQUAL((size_t)2, configs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("1.1.1.1"), configs[0].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)443, configs[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), configs[0].path);
  CPPUNIT_ASSERT_EQUAL(std::string("2606:4700:4700::1111"),
                       configs[1].connectHost);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, configs[1].port);
  CPPUNIT_ASSERT_EQUAL(std::string("/dns-query"), configs[1].path);
}

void AsyncDnsServerConfigTest::testParseDohServerConfig_badInput()
{
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig(""), Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("1.1.1.1/dns-query"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("http://1.1.1.1/dns-query"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://1.1.1.1"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://1.1.1.1:/x"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://1.1.1.1:0/x"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://1.1.1.1:65536/x"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://user@1.1.1.1/x"),
                       Exception);
  CPPUNIT_ASSERT_THROW(parseAsyncDnsDohServerConfig("https://1.1.1.1/x#frag"),
                       Exception);
  CPPUNIT_ASSERT_THROW(
      parseAsyncDnsDohServerConfig("https://2606:4700:4700::1111/dns-query"),
      Exception);
  CPPUNIT_ASSERT_THROW(
      parseAsyncDnsDohServerConfigList("https://1.1.1.1/dns-query,"),
      Exception);
}

void AsyncDnsServerConfigTest::testValidateDohServerConfigForDirectConnect()
{
  auto configs = parseAsyncDnsDohServerConfigList(
      "https://1.1.1.1/dns-query,https://[2606:4700:4700::1111]/dns-query");
  validateAsyncDnsDohServerConfigForDirectConnect(configs);

  configs = parseAsyncDnsDohServerConfigList("https://dns.example.org/dns-query");
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), configs[0].connectHost);
  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDohServerConfigForDirectConnect(configs), Exception);

  configs = parseAsyncDnsDohServerConfigList(
      "https://1.1.1.1/dns-query,https://dns.example.org/dns-query");
  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDohServerConfigForDirectConnect(configs), Exception);

  CPPUNIT_ASSERT_THROW(
      validateAsyncDnsDohServerConfigForDirectConnect(
          std::vector<AsyncDohServerConfig>()),
      Exception);
}

} // namespace aria2
