#include "Logger.h"

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class LoggerTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(LoggerTest);
  CPPUNIT_TEST(testShouldLogNetworkRequiresOutputSink);
  CPPUNIT_TEST(testShouldLogNetworkWithConsoleNetworkMode);
  CPPUNIT_TEST_SUITE_END();

public:
  void testShouldLogNetworkRequiresOutputSink();
  void testShouldLogNetworkWithConsoleNetworkMode();
};

CPPUNIT_TEST_SUITE_REGISTRATION(LoggerTest);

void LoggerTest::testShouldLogNetworkRequiresOutputSink()
{
  Logger logger;
  logger.setConsoleOutput(false);
  logger.setNetworkLogEnabled(true);

  CPPUNIT_ASSERT(!logger.shouldLogNetwork());
  logger.logNetwork(__FILE__, __LINE__, "network log");
}

void LoggerTest::testShouldLogNetworkWithConsoleNetworkMode()
{
  Logger logger;
  logger.setConsoleOutput(true);
  logger.setNetworkConsoleLogEnabled(true);

  CPPUNIT_ASSERT(logger.shouldLogNetwork());
}

} // namespace aria2
