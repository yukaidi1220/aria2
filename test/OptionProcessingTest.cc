#include "common.h"

#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "BufferedFile.h"
#include "File.h"
#include "Option.h"
#include "OptionParser.h"
#include "prefs.h"
#include "util.h"
#include "error_code.h"

namespace aria2 {

extern error_code::Value option_processing(Option& option, bool standalone,
                                           std::vector<std::string>& uris,
                                           int argc, char** argv,
                                           const KeyVals& options);

namespace {

std::string testFile(const std::string& name)
{
  return util::applyDir(A2_TEST_OUT_DIR, name);
}

void writeFile(const std::string& path, const std::string& data)
{
  File(File(path).getDirname()).mkdirs();
  BufferedFile fp(path.c_str(), BufferedFile::WRITE);
  fp.write(data.c_str(), data.size());
}

} // namespace

class OptionProcessingTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(OptionProcessingTest);
  CPPUNIT_TEST(testCommandLineOverridesConfByDefault);
  CPPUNIT_TEST(testConfCanOverrideCommandLine);
  CPPUNIT_TEST(testNoConfSkipsExplicitConfPath);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}
  void tearDown() {}

  void testCommandLineOverridesConfByDefault();
  void testConfCanOverrideCommandLine();
  void testNoConfSkipsExplicitConfPath();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OptionProcessingTest);

void OptionProcessingTest::testCommandLineOverridesConfByDefault()
{
  auto confPath = testFile("aria2_OptionProcessingTest_default.conf");
  File(confPath).remove();
  writeFile(confPath, "split=4\n");

  Option option;
  std::vector<std::string> uris;
  KeyVals options{{"conf-path", confPath}, {"split", "8"}};

  auto rv = option_processing(option, false, uris, 0, nullptr, options);

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("8"), option.get(PREF_SPLIT));
  CPPUNIT_ASSERT_EQUAL(std::string("command"), option.get(PREF_CONF_PRECEDENCE));
  File(confPath).remove();
}

void OptionProcessingTest::testConfCanOverrideCommandLine()
{
  auto confPath = testFile("aria2_OptionProcessingTest_conf.conf");
  File(confPath).remove();
  writeFile(confPath,
            "split=4\nconf-precedence=conf\nconf-path=ignored.conf\n"
            "no-conf=true\n");

  Option option;
  std::vector<std::string> uris;
  KeyVals options{{"conf-path", confPath}, {"split", "8"}};

  auto rv = option_processing(option, false, uris, 0, nullptr, options);

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("4"), option.get(PREF_SPLIT));
  CPPUNIT_ASSERT_EQUAL(std::string("conf"), option.get(PREF_CONF_PRECEDENCE));
  CPPUNIT_ASSERT_EQUAL(confPath, option.get(PREF_CONF_PATH));
  CPPUNIT_ASSERT_EQUAL(A2_V_FALSE, option.get(PREF_NO_CONF));
  File(confPath).remove();
}

void OptionProcessingTest::testNoConfSkipsExplicitConfPath()
{
  auto confPath = testFile("aria2_OptionProcessingTest_no_conf.conf");
  File(confPath).remove();
  writeFile(confPath, "split=4\n");

  Option option;
  std::vector<std::string> uris;
  KeyVals options{{"no-conf", A2_V_TRUE}, {"conf-path", confPath}};

  auto rv = option_processing(option, false, uris, 0, nullptr, options);

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("16"), option.get(PREF_SPLIT));
  File(confPath).remove();
}

} // namespace aria2
