#include "HttpSkipResponseCommand.h"

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class HttpSkipResponseCommandTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpSkipResponseCommandTest);
  CPPUNIT_TEST(testShouldRetryHttpStatusByDefault);
  CPPUNIT_TEST_SUITE_END();

public:
  void testShouldRetryHttpStatusByDefault();
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpSkipResponseCommandTest);

void HttpSkipResponseCommandTest::testShouldRetryHttpStatusByDefault()
{
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(200));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(302));

  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(400));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(405));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(407));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(410));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(429));
  CPPUNIT_ASSERT(shouldRetryHttpStatusByDefault(500));

  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(401));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(403));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(404));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(416));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(502));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(503));
  CPPUNIT_ASSERT(!shouldRetryHttpStatusByDefault(504));
}

} // namespace aria2
