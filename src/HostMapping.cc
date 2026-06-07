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
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "HostMapping.h"

#include <iterator>
#include <utility>

#include "DlAbortEx.h"
#include "Option.h"
#include "fmt.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

namespace {

struct HostMappingEntry {
  std::string sourceHost;
  std::string mappedHost;
};

std::string normalizeHost(std::string host)
{
  if (host.size() >= 2 && host[0] == '[' && host[host.size() - 1] == ']') {
    host = host.substr(1, host.size() - 2);
  }
  return util::toLower(std::move(host));
}

void throwBadHostMapping(const std::string& value)
{
  throw DL_ABORT_EX(fmt("Bad host mapping '%s'", value.c_str()));
}

std::string parseRightHostToken(const std::string& value)
{
  auto token = util::strip(value);
  if (token.empty()) {
    return token;
  }
  if (token[0] == '[') {
    if (token.size() < 3 || token[token.size() - 1] != ']') {
      throwBadHostMapping(value);
    }
    token = token.substr(1, token.size() - 2);
  }
  else if (token.find(':') != std::string::npos) {
    throwBadHostMapping(value);
  }
  return token;
}

HostMappingEntry parseHostMappingEntry(const std::string& value)
{
  auto entry = util::strip(value);
  if (entry.empty()) {
    throwBadHostMapping(value);
  }

  std::string sourceHost;
  std::string mappedHost;
  if (entry[0] == '[') {
    auto closeBracket = entry.find(']');
    if (closeBracket == std::string::npos || closeBracket == 1) {
      throwBadHostMapping(value);
    }
    auto delim = entry.find(':', closeBracket + 1);
    if (delim == std::string::npos ||
        !util::strip(entry.substr(closeBracket + 1,
                                  delim - closeBracket - 1))
             .empty()) {
      throwBadHostMapping(value);
    }
    sourceHost = entry.substr(1, closeBracket - 1);
    mappedHost = parseRightHostToken(entry.substr(delim + 1));
  }
  else {
    auto delim = entry.find(':');
    if (delim == std::string::npos) {
      throwBadHostMapping(value);
    }
    sourceHost = util::strip(entry.substr(0, delim));
    mappedHost = parseRightHostToken(entry.substr(delim + 1));
  }

  if (sourceHost.empty() || mappedHost.empty()) {
    throwBadHostMapping(value);
  }

  const auto sourceIsNumeric = util::isNumericHost(sourceHost);
  const auto mappedIsNumeric = util::isNumericHost(mappedHost);
  if (sourceIsNumeric == mappedIsNumeric) {
    throwBadHostMapping(value);
  }

  return {normalizeHost(std::move(sourceHost)),
          mappedIsNumeric ? std::move(mappedHost)
                          : normalizeHost(std::move(mappedHost))};
}

std::vector<HostMappingEntry> parseHostMapping(const Option* option)
{
  std::vector<HostMappingEntry> result;
  if (option == nullptr || !option->defined(PREF_HOSTS_MAPPING)) {
    return result;
  }

  std::vector<std::string> entries;
  const auto& value = option->get(PREF_HOSTS_MAPPING);
  util::split(std::begin(value), std::end(value), std::back_inserter(entries),
              ',', true, true);
  for (const auto& entry : entries) {
    result.push_back(parseHostMappingEntry(entry));
  }
  return result;
}

} // namespace

std::vector<std::string> getMappedAddresses(const std::string& hostname,
                                            const Option* option)
{
  std::vector<std::string> result;
  const auto normalizedHostname = normalizeHost(hostname);
  for (const auto& entry : parseHostMapping(option)) {
    if (entry.sourceHost == normalizedHostname &&
        util::isNumericHost(entry.mappedHost)) {
      result.push_back(entry.mappedHost);
    }
  }
  return result;
}

std::string getLogicalHostForRequest(const std::string& requestHost,
                                     const Option* option)
{
  const auto normalizedRequestHost = normalizeHost(requestHost);
  for (const auto& entry : parseHostMapping(option)) {
    if (entry.sourceHost == normalizedRequestHost &&
        !util::isNumericHost(entry.mappedHost)) {
      return entry.mappedHost;
    }
  }
  return requestHost;
}

} // namespace aria2
