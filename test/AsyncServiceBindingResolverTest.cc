#include "AsyncServiceBindingResolver.h"

#include <string>
#include <utility>

#include <cppunit/extensions/HelperMacros.h>

#include "File.h"
#include "LogFactory.h"
#include "Logger.h"
#include "TestUtil.h"
#include "prefs.h"

namespace aria2 {

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

class AsyncServiceBindingResolverTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(AsyncServiceBindingResolverTest);
  CPPUNIT_TEST(testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort);
  CPPUNIT_TEST(testCreateHttpsServiceBindingQueryNameUsesPortPrefix);
  CPPUNIT_TEST(testLogsSystemDnsServerSource);
  CPPUNIT_TEST(testLogsConfiguredDnsServerSource);
  CPPUNIT_TEST(testRejectedServerLogsSystemFallback);
#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
  CPPUNIT_TEST(testLogsTcpTransport);
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS
  CPPUNIT_TEST_SUITE_END();

public:
  void testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort();
  void testCreateHttpsServiceBindingQueryNameUsesPortPrefix();
  void testLogsSystemDnsServerSource();
  void testLogsConfiguredDnsServerSource();
  void testRejectedServerLogsSystemFallback();
#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
  void testLogsTcpTransport();
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS
};

CPPUNIT_TEST_SUITE_REGISTRATION(AsyncServiceBindingResolverTest);

void AsyncServiceBindingResolverTest::
    testCreateHttpsServiceBindingQueryNameUsesHostForDefaultPort()
{
  CPPUNIT_ASSERT_EQUAL(
      std::string("www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 443));
  CPPUNIT_ASSERT_EQUAL(
      std::string("www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 0));
}

void AsyncServiceBindingResolverTest::
    testCreateHttpsServiceBindingQueryNameUsesPortPrefix()
{
  CPPUNIT_ASSERT_EQUAL(
      std::string("_8443._https.www.example.com"),
      createHttpsServiceBindingQueryName("www.example.com", 8443));
}

void AsyncServiceBindingResolverTest::testLogsSystemDnsServerSource()
{
  auto logPath = std::string(A2_TEST_OUT_DIR) +
                 "/aria2_AsyncServiceBindingResolverTest_system.log";
  ScopedNetworkLog log(logPath);

  AsyncServiceBindingResolver resolver;
  resolver.resolve("www.example.com", 443);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: HTTPS RR c-ares using system DNS over UDP") !=
                 std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: query HTTPS RR www.example.com using c-ares "
                           "server_source=system servers=system "
                           "transport=UDP") != std::string::npos);
}

void AsyncServiceBindingResolverTest::testLogsConfiguredDnsServerSource()
{
  auto logPath = std::string(A2_TEST_OUT_DIR) +
                 "/aria2_AsyncServiceBindingResolverTest_configured.log";
  ScopedNetworkLog log(logPath);

  AsyncServiceBindingResolver resolver("192.0.2.53");
  resolver.resolve("www.example.com", 8443);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: HTTPS RR c-ares using configured servers "
                           "192.0.2.53 over UDP") != std::string::npos);
  CPPUNIT_ASSERT(
      logs.find("DNS: query HTTPS RR _8443._https.www.example.com using "
                "c-ares server_source=configured servers=192.0.2.53 "
                "transport=UDP") != std::string::npos);
}

void AsyncServiceBindingResolverTest::testRejectedServerLogsSystemFallback()
{
  auto logPath = std::string(A2_TEST_OUT_DIR) +
                 "/aria2_AsyncServiceBindingResolverTest_rejected.log";
  ScopedNetworkLog log(logPath);

  AsyncServiceBindingResolver resolver("dns.example.org");
  resolver.resolve("www.example.com", 443);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: HTTPS RR c-ares rejected configured server "
                           "list dns.example.org:") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: query HTTPS RR www.example.com using c-ares "
                           "server_source=system servers=system "
                           "transport=UDP") != std::string::npos);
}

#if defined(ARES_FLAG_USEVC) && defined(ARES_OPT_FLAGS)
void AsyncServiceBindingResolverTest::testLogsTcpTransport()
{
  auto logPath = std::string(A2_TEST_OUT_DIR) +
                 "/aria2_AsyncServiceBindingResolverTest_tcp.log";
  ScopedNetworkLog log(logPath);

  AsyncServiceBindingResolver resolver("192.0.2.53", true);
  resolver.resolve("www.example.com", 443);

  auto logs = log.closeAndRead();
  CPPUNIT_ASSERT(logs.find("DNS: HTTPS RR c-ares resolver forcing TCP "
                           "transport") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("DNS: HTTPS RR c-ares using configured servers "
                           "192.0.2.53 over TCP") != std::string::npos);
  CPPUNIT_ASSERT(logs.find("server_source=configured servers=192.0.2.53 "
                           "transport=TCP") != std::string::npos);
}
#endif // ARES_FLAG_USEVC && ARES_OPT_FLAGS

} // namespace aria2
