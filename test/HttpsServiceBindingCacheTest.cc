#include "HttpsServiceBindingCache.h"

#include <chrono>

#include <cppunit/extensions/HelperMacros.h>

#include "wallclock.h"

namespace aria2 {

class HttpsServiceBindingCacheTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(HttpsServiceBindingCacheTest);
  CPPUNIT_TEST(testFindReturnsCachedRecords);
  CPPUNIT_TEST(testKeepsRawRecordFields);
  CPPUNIT_TEST(testCacheKeepsPortsSeparate);
  CPPUNIT_TEST(testCacheKeepsHostsSeparate);
  CPPUNIT_TEST(testExpires);
  CPPUNIT_TEST(testZeroTtlDoesNotCache);
  CPPUNIT_TEST(testRemove);
  CPPUNIT_TEST(testResolvingState);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() { global::wallclock().reset(); }

  void testFindReturnsCachedRecords();
  void testKeepsRawRecordFields();
  void testCacheKeepsPortsSeparate();
  void testCacheKeepsHostsSeparate();
  void testExpires();
  void testZeroTtlDoesNotCache();
  void testRemove();
  void testResolvingState();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpsServiceBindingCacheTest);

namespace {

std::vector<dns::ServiceBindingRecord> createRecords(
    const std::string& targetName, uint16_t port, const std::string& alpn)
{
  dns::ServiceBindingRecord record;
  record.priority = 1;
  record.targetName = targetName;
  record.hasPort = true;
  record.port = port;
  record.alpn.push_back(alpn);

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(record);
  return records;
}

} // namespace

void HttpsServiceBindingCacheTest::testFindReturnsCachedRecords()
{
  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443,
              createRecords("svc.example.com", 8443, "h2"), 60);

  auto result = cache.find("www.example.com", 443);
  CPPUNIT_ASSERT(result);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result->size());
  CPPUNIT_ASSERT_EQUAL(std::string("svc.example.com"), (*result)[0].targetName);
  CPPUNIT_ASSERT((*result)[0].hasPort);
  CPPUNIT_ASSERT_EQUAL((uint16_t)8443, (*result)[0].port);
  CPPUNIT_ASSERT_EQUAL(std::string("h2"), (*result)[0].alpn[0]);
}

void HttpsServiceBindingCacheTest::testKeepsRawRecordFields()
{
  dns::ServiceBindingRecord record;
  record.priority = 3;
  record.targetName = "svc.example.com";
  record.echConfigList = "ech";
  record.ipv4hint.push_back("192.0.2.1");
  record.ipv6hint.push_back("2001:db8::1");

  std::vector<dns::ServiceBindingRecord> records;
  records.push_back(record);

  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443, records, 60);

  auto result = cache.find("www.example.com", 443);
  CPPUNIT_ASSERT(result);
  CPPUNIT_ASSERT_EQUAL((size_t)1, result->size());
  CPPUNIT_ASSERT_EQUAL((uint16_t)3, (*result)[0].priority);
  CPPUNIT_ASSERT_EQUAL(std::string("ech"), (*result)[0].echConfigList);
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"), (*result)[0].ipv4hint[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::1"), (*result)[0].ipv6hint[0]);
}

void HttpsServiceBindingCacheTest::testCacheKeepsPortsSeparate()
{
  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443,
              createRecords("https.example.com", 443, "h2"), 60);
  cache.cache("www.example.com", 8443,
              createRecords("alt.example.com", 8443, "http/1.1"), 60);

  auto https = cache.find("www.example.com", 443);
  auto alt = cache.find("www.example.com", 8443);
  CPPUNIT_ASSERT(https);
  CPPUNIT_ASSERT(alt);
  CPPUNIT_ASSERT_EQUAL(std::string("https.example.com"),
                       (*https)[0].targetName);
  CPPUNIT_ASSERT_EQUAL(std::string("alt.example.com"), (*alt)[0].targetName);
}

void HttpsServiceBindingCacheTest::testCacheKeepsHostsSeparate()
{
  HttpsServiceBindingCache cache;
  cache.cache("Example.com", 443,
              createRecords("upper.example.com", 443, "h2"), 60);
  cache.cache("example.com", 443,
              createRecords("lower.example.com", 443, "h2"), 60);

  auto upper = cache.find("Example.com", 443);
  auto lower = cache.find("example.com", 443);
  CPPUNIT_ASSERT(upper);
  CPPUNIT_ASSERT(lower);
  CPPUNIT_ASSERT_EQUAL(std::string("upper.example.com"),
                       (*upper)[0].targetName);
  CPPUNIT_ASSERT_EQUAL(std::string("lower.example.com"),
                       (*lower)[0].targetName);
}

void HttpsServiceBindingCacheTest::testExpires()
{
  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443,
              createRecords("svc.example.com", 443, "h2"), 1);

  CPPUNIT_ASSERT(cache.find("www.example.com", 443));

  global::wallclock().advance(std::chrono::seconds(1));

  CPPUNIT_ASSERT(!cache.find("www.example.com", 443));
}

void HttpsServiceBindingCacheTest::testZeroTtlDoesNotCache()
{
  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443,
              createRecords("svc.example.com", 443, "h2"), 0);

  CPPUNIT_ASSERT(!cache.find("www.example.com", 443));
}

void HttpsServiceBindingCacheTest::testRemove()
{
  HttpsServiceBindingCache cache;
  cache.cache("www.example.com", 443,
              createRecords("svc.example.com", 443, "h2"), 60);

  cache.remove("www.example.com", 443);

  CPPUNIT_ASSERT(!cache.find("www.example.com", 443));
}

void HttpsServiceBindingCacheTest::testResolvingState()
{
  HttpsServiceBindingCache cache;

  CPPUNIT_ASSERT(!cache.isResolving("www.example.com", 443));
  CPPUNIT_ASSERT(cache.markResolving("www.example.com", 443));
  CPPUNIT_ASSERT(cache.isResolving("www.example.com", 443));
  CPPUNIT_ASSERT(!cache.markResolving("www.example.com", 443));
  CPPUNIT_ASSERT(!cache.isResolving("www.example.com", 8443));

  cache.finishResolving("www.example.com", 443);

  CPPUNIT_ASSERT(!cache.isResolving("www.example.com", 443));
  CPPUNIT_ASSERT(cache.markResolving("www.example.com", 443));
}

} // namespace aria2
