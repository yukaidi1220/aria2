#include "DNSCache.h"

#include <cppunit/extensions/HelperMacros.h>
#include <iterator>
#include <vector>

namespace aria2 {

class DNSCacheTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(DNSCacheTest);
  CPPUNIT_TEST(testFind);
  CPPUNIT_TEST(testFindAll);
  CPPUNIT_TEST(testPutReturnsWhetherAddressAdded);
  CPPUNIT_TEST(testMarkBad);
  CPPUNIT_TEST(testPutBadAddr);
  CPPUNIT_TEST(testRemove);
  CPPUNIT_TEST_SUITE_END();

  DNSCache cache_;

public:
  void setUp()
  {
    cache_ = DNSCache();
    cache_.put("www", "192.168.0.1", 80);
    cache_.put("www", "::1", 80);
    cache_.put("ftp", "192.168.0.1", 21);
    cache_.put("proxy", "192.168.1.2", 8080);
  }

  void testFind();
  void testFindAll();
  void testPutReturnsWhetherAddressAdded();
  void testMarkBad();
  void testPutBadAddr();
  void testRemove();
};

CPPUNIT_TEST_SUITE_REGISTRATION(DNSCacheTest);

void DNSCacheTest::testFind()
{
  CPPUNIT_ASSERT_EQUAL(std::string("192.168.0.1"), cache_.find("www", 80));
  CPPUNIT_ASSERT_EQUAL(std::string("192.168.0.1"), cache_.find("ftp", 21));
  CPPUNIT_ASSERT_EQUAL(std::string("192.168.1.2"), cache_.find("proxy", 8080));
  CPPUNIT_ASSERT_EQUAL(std::string(""), cache_.find("www", 8080));
  CPPUNIT_ASSERT_EQUAL(std::string(""), cache_.find("another", 80));
}

void DNSCacheTest::testFindAll()
{
  std::vector<std::string> addrs;
  cache_.findAll(std::back_inserter(addrs), "www", 80);

  CPPUNIT_ASSERT_EQUAL((size_t)2, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.168.0.1"), addrs[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("::1"), addrs[1]);

  cache_.markBad("www", "192.168.0.1", 80);
  addrs.clear();
  cache_.findAll(std::back_inserter(addrs), "www", 80);

  CPPUNIT_ASSERT_EQUAL((size_t)1, addrs.size());
  CPPUNIT_ASSERT_EQUAL(std::string("::1"), addrs[0]);
}

void DNSCacheTest::testPutReturnsWhetherAddressAdded()
{
  CPPUNIT_ASSERT(!cache_.put("www", "192.168.0.1", 80));
  CPPUNIT_ASSERT(cache_.put("www", "192.168.0.2", 80));
  CPPUNIT_ASSERT(!cache_.put("www", "192.168.0.2", 80));
  CPPUNIT_ASSERT(cache_.put("www", "192.168.0.2", 443));
}

void DNSCacheTest::testMarkBad()
{
  cache_.markBad("www", "192.168.0.1", 80);
  CPPUNIT_ASSERT_EQUAL(std::string("::1"), cache_.find("www", 80));
}

void DNSCacheTest::testPutBadAddr()
{
  cache_.markBad("www", "192.168.0.1", 80);
  cache_.put("www", "192.168.0.1", 80);
  CPPUNIT_ASSERT_EQUAL(std::string("::1"), cache_.find("www", 80));
}

void DNSCacheTest::testRemove()
{
  cache_.remove("www", 80);
  CPPUNIT_ASSERT_EQUAL(std::string(""), cache_.find("www", 80));
}

} // namespace aria2
