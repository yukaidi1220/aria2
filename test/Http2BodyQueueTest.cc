#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2BodyQueue.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class Http2BodyQueueTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2BodyQueueTest);
  CPPUNIT_TEST(testPushPopChunks);
  CPPUNIT_TEST(testPushRejectsOverflow);
  CPPUNIT_TEST(testPushRejectsClosedQueue);
  CPPUNIT_TEST(testDrainAll);
  CPPUNIT_TEST(testClear);
  CPPUNIT_TEST(testCloseStoresErrorCode);
  CPPUNIT_TEST_SUITE_END();

public:
  void testPushPopChunks();
  void testPushRejectsOverflow();
  void testPushRejectsClosedQueue();
  void testDrainAll();
  void testClear();
  void testCloseStoresErrorCode();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2BodyQueueTest);

void Http2BodyQueueTest::testPushPopChunks()
{
  Http2BodyQueue queue(16);
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("abc"), 3));
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("defg"), 4));

  CPPUNIT_ASSERT_EQUAL((size_t)7, queue.size());
  CPPUNIT_ASSERT(queue.pop(0).empty());
  CPPUNIT_ASSERT_EQUAL(std::string("abcd"), queue.pop(4));
  CPPUNIT_ASSERT_EQUAL((size_t)3, queue.size());
  CPPUNIT_ASSERT_EQUAL(std::string("efg"), queue.pop(16));
  CPPUNIT_ASSERT(queue.empty());
}

void Http2BodyQueueTest::testPushRejectsOverflow()
{
  Http2BodyQueue queue(4);
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("abc"), 3));
  CPPUNIT_ASSERT(!queue.push(
      reinterpret_cast<const unsigned char*>("de"), 2));
  CPPUNIT_ASSERT_EQUAL((size_t)3, queue.size());
  CPPUNIT_ASSERT_EQUAL(std::string("abc"), queue.drainAll());
}

void Http2BodyQueueTest::testPushRejectsClosedQueue()
{
  Http2BodyQueue queue(8);
  queue.close(0);

  CPPUNIT_ASSERT(!queue.push(
      reinterpret_cast<const unsigned char*>("a"), 1));
  CPPUNIT_ASSERT(queue.empty());
}

void Http2BodyQueueTest::testDrainAll()
{
  Http2BodyQueue queue(16);
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("hello"), 5));
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("world"), 5));

  CPPUNIT_ASSERT_EQUAL(std::string("helloworld"), queue.drainAll());
  CPPUNIT_ASSERT(queue.empty());
}

void Http2BodyQueueTest::testClear()
{
  Http2BodyQueue queue(16);
  CPPUNIT_ASSERT(queue.push(
      reinterpret_cast<const unsigned char*>("hello"), 5));

  queue.clear();

  CPPUNIT_ASSERT(queue.empty());
  CPPUNIT_ASSERT_EQUAL((size_t)16, queue.available());
}

void Http2BodyQueueTest::testCloseStoresErrorCode()
{
  Http2BodyQueue queue;
  queue.close(123);

  CPPUNIT_ASSERT(queue.closed());
  CPPUNIT_ASSERT_EQUAL((uint32_t)123, queue.errorCode());
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
