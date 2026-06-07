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
  CPPUNIT_TEST_SUITE_END();

public:
  void testParseDotServerConfig();
  void testParseDotServerConfigList();
  void testParseDotServerConfig_badInput();
  void testValidateDotServerConfigForDirectConnect();
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

} // namespace aria2
