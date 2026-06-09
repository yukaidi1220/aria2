/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 Tatsuhiro Tsujikawa
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
#include "AltSvcParser.h"

#include <limits>
#include <utility>

#include "util.h"

namespace aria2 {

AltSvcEntry::AltSvcEntry() : port(0), maxAge(86400), persist(false) {}

AltSvcHeader::AltSvcHeader() : clear(false) {}

namespace {

bool isOWS(char c)
{
  return c == ' ' || c == '\t';
}

size_t skipOWS(const std::string& s, size_t pos)
{
  for (; pos < s.size() && isOWS(s[pos]); ++pos)
    ;
  return pos;
}

std::string stripOWS(const std::string& s)
{
  auto first = skipOWS(s, 0);
  auto last = s.size();
  for (; last > first && isOWS(s[last - 1]); --last)
    ;
  return s.substr(first, last - first);
}

bool isTokenChar(char c)
{
  return ('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') ||
         ('a' <= c && c <= 'z') || c == '!' || c == '#' || c == '$' ||
         c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
         c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
         c == '|' || c == '~';
}

bool isH3ProtocolId(const std::string& protocolId)
{
  return protocolId == "h3";
}

bool parseToken(std::string& token, const std::string& s, size_t& pos)
{
  auto first = pos;
  for (; pos < s.size() && isTokenChar(s[pos]); ++pos)
    ;
  if (first == pos) {
    return false;
  }
  token.assign(s.begin() + first, s.begin() + pos);
  return true;
}

bool parseQuotedString(std::string& value, const std::string& s, size_t& pos)
{
  if (pos >= s.size() || s[pos] != '"') {
    return false;
  }
  ++pos;
  value.clear();
  for (; pos < s.size(); ++pos) {
    auto c = s[pos];
    if (c == '"') {
      ++pos;
      return true;
    }
    if (c == '\\') {
      ++pos;
      if (pos == s.size()) {
        return false;
      }
      c = s[pos];
    }
    value += c;
  }
  return false;
}

std::vector<std::string> splitAltSvcItems(const std::string& headerValue)
{
  std::vector<std::string> items;
  size_t first = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0; i < headerValue.size(); ++i) {
    auto c = headerValue[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quoted && c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && c == ',') {
      items.push_back(stripOWS(headerValue.substr(first, i - first)));
      first = i + 1;
    }
  }
  items.push_back(stripOWS(headerValue.substr(first)));
  return items;
}

std::vector<std::string> splitParams(const std::string& item)
{
  std::vector<std::string> params;
  size_t first = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0; i < item.size(); ++i) {
    auto c = item[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quoted && c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && c == ';') {
      params.push_back(stripOWS(item.substr(first, i - first)));
      first = i + 1;
    }
  }
  params.push_back(stripOWS(item.substr(first)));
  return params;
}

bool parseUInt16(uint16_t& out, const std::string& s)
{
  if (s.empty() || !util::isNumber(s.begin(), s.end())) {
    return false;
  }
  uint32_t n;
  if (!util::parseUIntNoThrow(n, s) || n > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  out = static_cast<uint16_t>(n);
  return true;
}

bool parseUInt64(uint64_t& out, const std::string& s)
{
  if (s.empty() || !util::isNumber(s.begin(), s.end())) {
    return false;
  }
  uint64_t n = 0;
  for (auto c : s) {
    auto digit = static_cast<uint64_t>(c - '0');
    if (n > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
      return false;
    }
    n = n * 10 + digit;
  }
  out = n;
  return true;
}

bool parseAuthority(AltSvcEntry& entry, const std::string& value)
{
  std::string host;
  std::string port;

  if (!value.empty() && value[0] == ':') {
    port = value.substr(1);
  }
  else if (!value.empty() && value[0] == '[') {
    auto closing = value.find(']');
    if (closing == std::string::npos || closing == 1 ||
        closing + 1 >= value.size() ||
        value[closing + 1] != ':') {
      return false;
    }
    host = value.substr(1, closing - 1);
    port = value.substr(closing + 2);
  }
  else {
    auto delim = value.rfind(':');
    if (delim == std::string::npos || delim == 0 ||
        delim == value.size() - 1) {
      return false;
    }
    host = value.substr(0, delim);
    if (host.find(':') != std::string::npos ||
        host.find_first_of(" \t\r\n") != std::string::npos) {
      return false;
    }
    port = value.substr(delim + 1);
  }

  uint16_t nport;
  if (!parseUInt16(nport, port) || nport == 0) {
    return false;
  }

  entry.host = std::move(host);
  entry.port = nport;
  return true;
}

bool parseParam(std::string& name, std::string& value, bool& hasValue,
                const std::string& param)
{
  size_t pos = 0;
  if (!parseToken(name, param, pos)) {
    return false;
  }
  pos = skipOWS(param, pos);
  if (pos == param.size()) {
    hasValue = false;
    value.clear();
    return true;
  }
  if (param[pos] != '=') {
    return false;
  }
  ++pos;
  pos = skipOWS(param, pos);
  hasValue = true;
  if (pos < param.size() && param[pos] == '"') {
    if (!parseQuotedString(value, param, pos)) {
      return false;
    }
  }
  else {
    if (!parseToken(value, param, pos)) {
      return false;
    }
  }
  pos = skipOWS(param, pos);
  return pos == param.size();
}

bool parseAltSvcItem(AltSvcEntry& entry, const std::string& item)
{
  auto params = splitParams(item);
  if (params.empty() || params[0].empty()) {
    return false;
  }

  std::string protocolId;
  std::string authority;
  bool hasValue = false;
  if (!parseParam(protocolId, authority, hasValue, params[0]) || !hasValue ||
      !isH3ProtocolId(protocolId)) {
    return false;
  }

  AltSvcEntry parsed;
  parsed.protocolId = std::move(protocolId);
  if (!parseAuthority(parsed, authority)) {
    return false;
  }

  for (size_t i = 1; i < params.size(); ++i) {
    if (params[i].empty()) {
      continue;
    }
    std::string name;
    std::string value;
    if (!parseParam(name, value, hasValue, params[i])) {
      continue;
    }
    name = util::toLower(std::move(name));
    if (name == "ma" && hasValue) {
      uint64_t maxAge;
      if (parseUInt64(maxAge, value)) {
        parsed.maxAge = maxAge;
      }
    }
    else if (name == "persist") {
      parsed.persist = hasValue && value == "1";
    }
  }

  entry = std::move(parsed);
  return true;
}

} // namespace

AltSvcHeader parseAltSvcHeader(const std::string& headerValue)
{
  AltSvcHeader result;
  for (const auto& item : splitAltSvcItems(headerValue)) {
    if (util::toLower(stripOWS(item)) == "clear") {
      result.clear = true;
      result.entries.clear();
      return result;
    }
    AltSvcEntry entry;
    if (parseAltSvcItem(entry, item)) {
      result.entries.push_back(std::move(entry));
    }
  }
  return result;
}

} // namespace aria2
