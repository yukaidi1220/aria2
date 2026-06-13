#include "common.h"

#ifdef __MINGW32__
#  include <direct.h>
#endif // __MINGW32__
#include <unistd.h>

#include <memory>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "array_fun.h"
#include "BufferedFile.h"
#include "File.h"
#include "Option.h"
#include "OptionParser.h"
#include "StartupOptionLog.h"
#include "prefs.h"
#include "util.h"
#include "error_code.h"

namespace aria2 {

extern error_code::Value option_processing(Option& option, bool standalone,
                                           std::vector<std::string>& uris,
                                           int argc, char** argv,
                                           const KeyVals& options);
std::vector<std::string> createDefaultConfigFileCandidates(
    const std::string& currentDir, const std::string& programDir,
    const std::string& userConfigFile);

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

int changeCurrentDir(const std::string& path)
{
#ifdef __MINGW32__
  return _wchdir(utf8ToWChar(path).c_str());
#else  // !__MINGW32__
  return chdir(path.c_str());
#endif // !__MINGW32__
}

class ScopedCurrentDir {
public:
  explicit ScopedCurrentDir(const std::string& path)
      : currentDir_(File::getCurrentDir())
  {
    File(path).mkdirs();
    CPPUNIT_ASSERT_EQUAL(0, changeCurrentDir(path));
  }

  ~ScopedCurrentDir() { changeCurrentDir(currentDir_); }

private:
  std::string currentDir_;
};

} // namespace

class OptionProcessingTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(OptionProcessingTest);
  CPPUNIT_TEST(testCreateDefaultConfigFileCandidates);
  CPPUNIT_TEST(testLoadsAria2ConfFromCurrentDirByDefault);
#ifndef __MINGW32__
  CPPUNIT_TEST(testLoadsAria2ConfFromProgramDirByDefault);
#endif // !__MINGW32__
  CPPUNIT_TEST(testRelativeConfPathResolvedFromCurrentDir);
  CPPUNIT_TEST(testNoConfSkipsCurrentDirAria2Conf);
  CPPUNIT_TEST(testCommandLineOverridesConfByDefault);
  CPPUNIT_TEST(testConfCanOverrideCommandLine);
  CPPUNIT_TEST(testNoConfSkipsExplicitConfPath);
  CPPUNIT_TEST(testStartupOptionLogSources);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() {}
  void tearDown() {}

  void testCreateDefaultConfigFileCandidates();
  void testLoadsAria2ConfFromCurrentDirByDefault();
#ifndef __MINGW32__
  void testLoadsAria2ConfFromProgramDirByDefault();
#endif // !__MINGW32__
  void testRelativeConfPathResolvedFromCurrentDir();
  void testNoConfSkipsCurrentDirAria2Conf();
  void testCommandLineOverridesConfByDefault();
  void testConfCanOverrideCommandLine();
  void testNoConfSkipsExplicitConfPath();
  void testStartupOptionLogSources();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OptionProcessingTest);

void OptionProcessingTest::testCreateDefaultConfigFileCandidates()
{
  auto candidates = createDefaultConfigFileCandidates(
      "/tmp/current", "/tmp/program", "/tmp/user/aria2.conf");

  CPPUNIT_ASSERT_EQUAL((size_t)3, candidates.size());
  CPPUNIT_ASSERT_EQUAL(std::string("/tmp/current/aria2.conf"), candidates[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("/tmp/program/aria2.conf"), candidates[1]);
  CPPUNIT_ASSERT_EQUAL(std::string("/tmp/user/aria2.conf"), candidates[2]);

  candidates = createDefaultConfigFileCandidates(
      "/tmp/current", "/tmp/current", "/tmp/current/aria2.conf");

  CPPUNIT_ASSERT_EQUAL((size_t)1, candidates.size());
  CPPUNIT_ASSERT_EQUAL(std::string("/tmp/current/aria2.conf"), candidates[0]);
}

void OptionProcessingTest::testLoadsAria2ConfFromCurrentDirByDefault()
{
  auto dir = testFile("aria2_OptionProcessingTest_default_current_dir");
  auto confPath = util::applyDir(dir, "aria2.conf");
  File(confPath).remove();
  writeFile(confPath, "split=4\n");
  ScopedCurrentDir cwd(dir);
  // After changing directory, option_processing uses getcwd() which
  // returns an absolute path, so build the expected path from the
  // actual current directory.
  auto absConfPath = util::applyDir(File::getCurrentDir(), "aria2.conf");

  Option option;
  std::vector<std::string> uris;

  auto rv = option_processing(option, false, uris, 0, nullptr, KeyVals{});

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("4"), option.get(PREF_SPLIT));
  CPPUNIT_ASSERT_EQUAL(absConfPath, option.get(PREF_CONF_PATH));
  File(confPath).remove();
}

#ifndef __MINGW32__
void OptionProcessingTest::testLoadsAria2ConfFromProgramDirByDefault()
{
  // Compute absolute paths BEFORE changing directory, so that
  // getProgramDir() returns an absolute path that option_processing
  // can resolve regardless of the current working directory.
  auto origCwd = File::getCurrentDir();
  auto currentDir =
      util::applyDir(origCwd,
                     testFile("aria2_OptionProcessingTest_empty_current_dir"));
  auto programDir =
      util::applyDir(origCwd,
                     testFile("aria2_OptionProcessingTest_program_dir"));
  auto currentConfPath = util::applyDir(currentDir, "aria2.conf");
  auto confPath = util::applyDir(programDir, "aria2.conf");
  File(currentConfPath).remove();
  File(confPath).remove();
  writeFile(confPath, "split=4\n");
  ScopedCurrentDir cwd(currentDir);

  auto programPath = util::applyDir(programDir, "fake-aria2");
  std::vector<char> arg0(programPath.begin(), programPath.end());
  arg0.push_back('\0');
  char* argv[] = {arg0.data(), nullptr};
  Option option;
  std::vector<std::string> uris;

  auto rv =
      option_processing(option, false, uris, 1, argv, KeyVals{});

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("4"), option.get(PREF_SPLIT));
  CPPUNIT_ASSERT_EQUAL(confPath, option.get(PREF_CONF_PATH));
  File(confPath).remove();
}
#endif // !__MINGW32__

void OptionProcessingTest::testRelativeConfPathResolvedFromCurrentDir()
{
  auto dir = testFile("aria2_OptionProcessingTest_relative_conf_path");
  auto confPath = util::applyDir(dir, "custom.conf");
  File(confPath).remove();
  writeFile(confPath, "split=4\n");
  ScopedCurrentDir cwd(dir);

  Option option;
  std::vector<std::string> uris;
  KeyVals options{{"conf-path", "custom.conf"}};

  auto rv = option_processing(option, false, uris, 0, nullptr, options);

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("4"), option.get(PREF_SPLIT));
  CPPUNIT_ASSERT_EQUAL(std::string("custom.conf"), option.get(PREF_CONF_PATH));
  File(confPath).remove();
}

void OptionProcessingTest::testNoConfSkipsCurrentDirAria2Conf()
{
  auto dir = testFile("aria2_OptionProcessingTest_no_conf_current_dir");
  auto confPath = util::applyDir(dir, "aria2.conf");
  File(confPath).remove();
  writeFile(confPath, "split=4\n");
  ScopedCurrentDir cwd(dir);

  Option option;
  std::vector<std::string> uris;
  KeyVals options{{"no-conf", A2_V_TRUE}};

  auto rv = option_processing(option, false, uris, 0, nullptr, options);

  CPPUNIT_ASSERT_EQUAL(error_code::FINISHED, rv);
  CPPUNIT_ASSERT_EQUAL(std::string("16"), option.get(PREF_SPLIT));
  File(confPath).remove();
}

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

void OptionProcessingTest::testStartupOptionLogSources()
{
  auto& parser = OptionParser::getInstance();
  auto defaults = std::make_shared<Option>();
  defaults->put(PREF_SPLIT, "16");
  defaults->put(PREF_CONF_PRECEDENCE, "command");
  Option conf;
  conf.setParent(defaults);
  conf.put(PREF_MIN_SPLIT_SIZE, "2M");
  auto command = std::make_shared<Option>();
  command->setParent(std::make_shared<Option>(conf));
  command->put(PREF_ASYNC_DNS, A2_V_TRUE);
  command->put(PREF_CONF_PRECEDENCE, "command");

  Option commandWins;
  commandWins.setParent(std::make_shared<Option>(conf));
  commandWins.merge(*command);

  CPPUNIT_ASSERT_EQUAL(
      std::string("Option: async-dns=true (source=command)"),
      formatStartupOptionLog(&commandWins, PREF_ASYNC_DNS, parser.get()));
  CPPUNIT_ASSERT_EQUAL(
      std::string("Option: min-split-size=2M (source=conf)"),
      formatStartupOptionLog(&commandWins, PREF_MIN_SPLIT_SIZE, parser.get()));
  CPPUNIT_ASSERT_EQUAL(std::string("default"),
                       getStartupOptionSource(&commandWins, PREF_SPLIT,
                                              parser.get()));

  defaults->put(PREF_SPLIT, "32");
  CPPUNIT_ASSERT_EQUAL(std::string("runtime"),
                       getStartupOptionSource(&commandWins, PREF_SPLIT,
                                              parser.get()));

  auto commandParent = std::make_shared<Option>();
  commandParent->setParent(defaults);
  commandParent->put(PREF_ASYNC_DNS, A2_V_TRUE);
  conf.put(PREF_CONF_PRECEDENCE, "conf");
  Option confWins;
  confWins.setParent(commandParent);
  confWins.merge(conf);

  CPPUNIT_ASSERT_EQUAL(std::string("command"),
                       getStartupOptionSource(&confWins, PREF_ASYNC_DNS,
                                              parser.get()));
  CPPUNIT_ASSERT_EQUAL(std::string("conf"),
                       getStartupOptionSource(&confWins, PREF_MIN_SPLIT_SIZE,
                                              parser.get()));
  CPPUNIT_ASSERT_EQUAL(
      std::string("Option: min-split-size=2M (source=conf)"),
      formatStartupOptionLog(&confWins, PREF_MIN_SPLIT_SIZE, parser.get()));
}

} // namespace aria2
