/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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
 * file(s) with this exception, you may extend this exception statement to your
 * version.  If you do not wish to do so, delete this exception statement from
 * your version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "AsyncDnsServerConfig.h"

#include <iterator>

#include "DlAbortEx.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace {
const uint16_t DEFAULT_DOT_PORT = 853;

void throwBadDotServerConfig(const std::string& value)
{
  throw DL_ABORT_EX(fmt("Bad async DNS DoT server '%s'", value.c_str()));
}

uint16_t parsePort(const std::string& port, const std::string& value)
{
  if (port.empty()) {
    throwBadDotServerConfig(value);
  }

  uint32_t n;
  if (!util::parseUIntNoThrow(n, port) || n == 0 || n > UINT16_MAX) {
    throwBadDotServerConfig(value);
  }
  return static_cast<uint16_t>(n);
}

std::string defaultTLSHost(const std::string& host)
{
  return util::isNumericHost(host) ? std::string() : host;
}
} // namespace

AsyncDnsServerConfig parseAsyncDnsDotServerConfig(const std::string& value)
{
  auto entry = util::strip(value);
  if (entry.empty()) {
    throwBadDotServerConfig(value);
  }

  std::string host;
  auto port = DEFAULT_DOT_PORT;
  if (entry[0] == '[') {
    auto closeBracket = entry.find(']');
    if (closeBracket == std::string::npos || closeBracket == 1) {
      throwBadDotServerConfig(value);
    }
    host = entry.substr(1, closeBracket - 1);
    auto rest = util::strip(entry.substr(closeBracket + 1));
    if (!rest.empty()) {
      if (rest[0] != ':') {
        throwBadDotServerConfig(value);
      }
      port = parsePort(util::strip(rest.substr(1)), value);
    }
  }
  else {
    auto delim = entry.find(':');
    if (delim == std::string::npos) {
      host = entry;
    }
    else {
      if (entry.find(':', delim + 1) != std::string::npos) {
        throwBadDotServerConfig(value);
      }
      host = util::strip(entry.substr(0, delim));
      port = parsePort(util::strip(entry.substr(delim + 1)), value);
    }
  }

  host = util::strip(host);
  if (host.empty()) {
    throwBadDotServerConfig(value);
  }

  return {host, port, defaultTLSHost(host)};
}

std::vector<AsyncDnsServerConfig>
parseAsyncDnsDotServerConfigList(const std::string& value)
{
  std::vector<AsyncDnsServerConfig> result;
  if (value.empty()) {
    return result;
  }

  std::vector<std::string> entries;
  util::split(std::begin(value), std::end(value), std::back_inserter(entries),
              ',', true, true);
  for (const auto& entry : entries) {
    result.push_back(parseAsyncDnsDotServerConfig(entry));
  }
  return result;
}

} // namespace aria2
