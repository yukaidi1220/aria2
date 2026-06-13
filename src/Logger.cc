/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2010 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "Logger.h"

#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cassert>

#include "DlAbortEx.h"
#include "fmt.h"
#include "message.h"
#include "A2STR.h"
#include "a2time.h"
#include "BufferedFile.h"
#include "util.h"
#include "console.h"

namespace aria2 {

Logger::Logger()
    : logLevel_(Logger::A2_DEBUG),
      consoleLogLevel_(Logger::A2_NOTICE),
      consoleOutput_(true),
      colorOutput_(global::cout()->supportsColor()),
      networkLogEnabled_(false),
      networkConsoleLogEnabled_(false)
{
}

Logger::~Logger() = default;

void Logger::openFile(const std::string& filename)
{
  closeFile();
  if (filename == DEV_STDOUT) {
    fpp_ = global::cout();
  }
  else {
    fpp_ =
        std::make_shared<BufferedFile>(filename.c_str(), BufferedFile::APPEND);
    if (!*static_cast<BufferedFile*>(fpp_.get())) {
      throw DL_ABORT_EX(fmt(EX_FILE_OPEN, filename.c_str(), "n/a"));
    }
  }
}

void Logger::closeFile()
{
  if (fpp_) {
    fpp_.reset();
  }
}

void Logger::setConsoleOutput(bool enabled) { consoleOutput_ = enabled; }

void Logger::setColorOutput(bool enabled) { colorOutput_ = enabled; }

bool Logger::fileLogEnabled(LEVEL level) { return level >= logLevel_ && fpp_; }

bool Logger::consoleLogEnabled(LEVEL level)
{
  return consoleOutput_ && level >= consoleLogLevel_;
}

bool Logger::levelEnabled(LEVEL level)
{
  return fileLogEnabled(level) || consoleLogEnabled(level);
}

namespace {
const char* levelToString(Logger::LEVEL level)
{
  switch (level) {
  case Logger::A2_DEBUG:
    return "DEBUG";
  case Logger::A2_INFO:
    return "INFO";
  case Logger::A2_NOTICE:
    return "NOTICE";
  case Logger::A2_WARN:
    return "WARN";
  case Logger::A2_ERROR:
    return "ERROR";
  default:
    return "";
  }
}
} // namespace

namespace {
template <typename Output>
void writeHeader(Output& fp, Logger::LEVEL level, const char* sourceFile,
                 int lineNum)
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  char datestr[20]; // 'YYYY-MM-DD hh:mm:ss'+'\0' = 20 bytes
  struct tm tm;
  // tv.tv_sec may not be of type time_t.
  time_t timesec = tv.tv_sec;
  localtime_r(&timesec, &tm);
  size_t dateLength =
      strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", &tm);
  assert(dateLength <= (size_t)20);
  fp.printf("%s.%06ld [%s] [%s:%d] ", datestr, (unsigned long)tv.tv_usec,
            levelToString(level), sourceFile, lineNum);
}
} // namespace

namespace {
const char* levelColor(Logger::LEVEL level)
{
  switch (level) {
  case Logger::A2_DEBUG:
    return "\033[1;37m";
  case Logger::A2_INFO:
    return "\033[1;36m";
  case Logger::A2_NOTICE:
    return "\033[1;32m";
  case Logger::A2_WARN:
    return "\033[1;33m";
  case Logger::A2_ERROR:
    return "\033[1;31m";
  default:
    return "";
  }
}
} // namespace

namespace {
template <typename Output>
void writeHeaderConsole(Output& fp, Logger::LEVEL level, bool useColor)
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  char datestr[15]; // 'MM/DD hh:mm:ss'+'\0' = 15 bytes
  struct tm tm;
  // tv.tv_sec may not be of type time_t.
  time_t timesec = tv.tv_sec;
  localtime_r(&timesec, &tm);
  size_t dateLength = strftime(datestr, sizeof(datestr), "%m/%d %H:%M:%S", &tm);
  assert(dateLength <= (size_t)15);
  if (useColor) {
    fp.printf("%s [%s%s\033[0m] ", datestr, levelColor(level),
              levelToString(level));
  }
  else {
    fp.printf("%s [%s] ", datestr, levelToString(level));
  }
}
} // namespace

namespace {
template <typename Output>
void writeStackTrace(Output& fp, const char* stackTrace)
{
  fp.write(stackTrace);
}
} // namespace

void Logger::writeLog(Logger::LEVEL level, const char* sourceFile, int lineNum,
                      const char* msg, const char* trace)
{
  if (fileLogEnabled(level)) {
    writeHeader(*fpp_, level, sourceFile, lineNum);
    fpp_->printf("%s\n", msg);
    writeStackTrace(*fpp_, trace);
    fpp_->flush();
  }
  if (consoleLogEnabled(level)) {
    global::cout()->printf("\n");
    writeHeaderConsole(*global::cout(), level, colorOutput_);
    global::cout()->printf("%s\n", msg);
    writeStackTrace(*global::cout(), trace);
    global::cout()->flush();
  }
}

void Logger::log(LEVEL level, const char* sourceFile, int lineNum,
                 const char* msg)
{
  writeLog(level, sourceFile, lineNum, msg, "");
}

void Logger::log(LEVEL level, const char* sourceFile, int lineNum,
                 const std::string& msg)
{
  log(level, sourceFile, lineNum, msg.c_str());
}

void Logger::log(LEVEL level, const char* sourceFile, int lineNum,
                 const char* msg, const Exception& ex)
{
  writeLog(level, sourceFile, lineNum, msg, ex.stackTrace().c_str());
}

void Logger::log(LEVEL level, const char* sourceFile, int lineNum,
                 const std::string& msg, const Exception& ex)
{
  log(level, sourceFile, lineNum, msg.c_str(), ex);
}

bool Logger::shouldLogNetwork()
{
  return fileLogEnabled(A2_DEBUG) || (networkLogEnabled_ && fpp_) ||
         consoleLogEnabled(A2_DEBUG) ||
         (networkConsoleLogEnabled_ && consoleOutput_);
}

void Logger::logInfoExceptNetwork(const char* sourceFile, int lineNum,
                                  const char* msg)
{
  // Output to file if file output is not in network mode and log level allows
  // INFO
  if (!networkLogEnabled_ && fileLogEnabled(A2_INFO)) {
    writeHeader(*fpp_, A2_INFO, sourceFile, lineNum);
    fpp_->printf("%s\n", msg);
    fpp_->flush();
  }
  // Output to console if console output is not in network mode and console
  // log level allows INFO
  if (!networkConsoleLogEnabled_ && consoleLogEnabled(A2_INFO)) {
    global::cout()->printf("\n");
    writeHeaderConsole(*global::cout(), A2_INFO, colorOutput_);
    global::cout()->printf("%s\n", msg);
    global::cout()->flush();
  }
}

void Logger::logInfoExceptNetwork(const char* sourceFile, int lineNum,
                                  const std::string& msg)
{
  logInfoExceptNetwork(sourceFile, lineNum, msg.c_str());
}

namespace {
// Write a file log header with [NETWORK] tag instead of a severity level.
template <typename Output>
void writeNetworkHeader(Output& fp, const char* sourceFile, int lineNum)
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  char datestr[20]; // 'YYYY-MM-DD hh:mm:ss'+'\0' = 20 bytes
  struct tm tm;
  time_t timesec = tv.tv_sec;
  localtime_r(&timesec, &tm);
  size_t dateLength =
      strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", &tm);
  assert(dateLength <= (size_t)20);
  fp.printf("%s.%06ld [NETWORK] [%s:%d] ", datestr,
            (unsigned long)tv.tv_usec, sourceFile, lineNum);
}

// Write a console log header with [NETWORK] tag.
template <typename Output>
void writeNetworkHeaderConsole(Output& fp, bool useColor)
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  char datestr[15]; // 'MM/DD hh:mm:ss'+'\0' = 15 bytes
  struct tm tm;
  time_t timesec = tv.tv_sec;
  localtime_r(&timesec, &tm);
  size_t dateLength =
      strftime(datestr, sizeof(datestr), "%m/%d %H:%M:%S", &tm);
  assert(dateLength <= (size_t)15);
  if (useColor) {
    fp.printf("%s [\033[1;35mNETWORK\033[0m] ", datestr);
  }
  else {
    fp.printf("%s [NETWORK] ", datestr);
  }
}
} // namespace

void Logger::logNetwork(const char* sourceFile, int lineNum, const char* msg)
{
  // Network mode can be enabled independently for file and console output.
  // Keep these checks in sync with shouldLogNetwork() so message formatting is
  // skipped when there is no sink.
  bool doFile = fileLogEnabled(A2_DEBUG) || (networkLogEnabled_ && fpp_);
  bool doConsole = consoleLogEnabled(A2_DEBUG) ||
                   (networkConsoleLogEnabled_ && consoleOutput_);
  if (doFile) {
    writeNetworkHeader(*fpp_, sourceFile, lineNum);
    fpp_->printf("%s\n", msg);
    fpp_->flush();
  }
  if (doConsole) {
    global::cout()->printf("\n");
    writeNetworkHeaderConsole(*global::cout(), colorOutput_);
    global::cout()->printf("%s\n", msg);
    global::cout()->flush();
  }
}

void Logger::logNetwork(const char* sourceFile, int lineNum,
                        const std::string& msg)
{
  logNetwork(sourceFile, lineNum, msg.c_str());
}

} // namespace aria2
