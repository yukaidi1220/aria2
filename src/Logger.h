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
#ifndef D_LOGGER_H
#define D_LOGGER_H

#include "common.h"

#include <string>
#include <memory>

namespace aria2 {

class Exception;
class OutputFile;

class Logger {
public:
  enum LEVEL {
    A2_DEBUG = 1 << 0,
    A2_INFO = 1 << 1,
    A2_NOTICE = 1 << 2,
    A2_WARN = 1 << 3,
    A2_ERROR = 1 << 4,
  };

private:
  // Minimum log level for file log output.
  LEVEL logLevel_;
  std::shared_ptr<OutputFile> fpp_;
  // Minimum log level for console log output.
  LEVEL consoleLogLevel_;
  // true if console log output is enabled.
  bool consoleOutput_;
  bool colorOutput_;
  // true if network log mode is enabled for file output.
  bool networkLogEnabled_;
  // true if network log mode is enabled for console output.
  bool networkConsoleLogEnabled_;
  // Don't allow copying
  Logger(const Logger&);
  Logger& operator=(const Logger&);

  void writeLog(Logger::LEVEL level, const char* sourceFile, int lineNum,
                const char* msg, const char* trace);

  // Returns true if message with log level |level| will be outputted
  // to file.
  bool fileLogEnabled(LEVEL level);
  // Returns true if message with log level |level| will be outputted
  // to console.
  bool consoleLogEnabled(LEVEL level);

public:
  Logger();

  ~Logger();

  void log(LEVEL level, const char* sourceFile, int lineNum, const char* msg);

  void log(LEVEL level, const char* sourceFile, int lineNum,
           const std::string& msg);

  void log(LEVEL level, const char* sourceFile, int lineNum, const char* msg,
           const Exception& ex);

  void log(LEVEL level, const char* sourceFile, int lineNum,
           const std::string& msg, const Exception& ex);

  void openFile(const std::string& filename);

  void closeFile();

  void setLogLevel(LEVEL level) { logLevel_ = level; }

  void setConsoleLogLevel(LEVEL level) { consoleLogLevel_ = level; }

  void setNetworkLogEnabled(bool enabled) { networkLogEnabled_ = enabled; }

  void setNetworkConsoleLogEnabled(bool enabled)
  {
    networkConsoleLogEnabled_ = enabled;
  }

  void setConsoleOutput(bool enabled);

  void setColorOutput(bool enabled);

  // Returns true if this logger actually writes debug log message to
  // either file or stdout.
  bool levelEnabled(LEVEL level);

  // Returns true if network log output should be performed for either
  // file or console.  Network logs are output when debug level is active or
  // when network mode is explicitly enabled and the corresponding sink exists.
  bool shouldLogNetwork();

  // Log a network-related message.  The message is output if debug level is
  // active or network mode is explicitly enabled for an available sink.
  void logNetwork(const char* sourceFile, int lineNum, const char* msg);
  void logNetwork(const char* sourceFile, int lineNum, const std::string& msg);

  // Log an INFO-level message that should be suppressed when network mode is
  // enabled for the corresponding sink.  The message is output to file if file
  // output is not in network mode and the log level allows INFO.  The message
  // is output to console if console output is not in network mode and the
  // console log level allows INFO.
  void logInfoExceptNetwork(const char* sourceFile, int lineNum,
                            const char* msg);
  void logInfoExceptNetwork(const char* sourceFile, int lineNum,
                            const std::string& msg);
};

} // namespace aria2

#endif // D_LOGGER_H
