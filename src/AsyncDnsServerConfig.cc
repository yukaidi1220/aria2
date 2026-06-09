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
#include <tuple>
#include <utility>

#include "DlAbortEx.h"
#include "fmt.h"
#include "uri.h"
#include "util.h"

namespace aria2 {

namespace {
const uint16_t DEFAULT_DOT_PORT = 853;
const uint16_t DEFAULT_DOH_PORT = 443;

void throwBadDotServerConfig(const std::string& value)
{
  throw DL_ABORT_EX(fmt("Bad async DNS DoT server '%s'", value.c_str()));
}

void throwBadDohServerConfig(const std::string& value)
{
  throw DL_ABORT_EX(fmt("Bad async DNS DoH server '%s'", value.c_str()));
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

bool isAsyncDnsTLSHost(const std::string& hostname)
{
  if (hostname.empty() || hostname.size() > 253 ||
      util::isNumericHost(hostname) ||
      hostname.find(".") == std::string::npos ||
      hostname[hostname.size() - 1] == '.') {
    return false;
  }

  std::string::size_type labelStart = 0;
  while (labelStart < hostname.size()) {
    auto labelEnd = hostname.find(".", labelStart);
    if (labelEnd == std::string::npos) {
      labelEnd = hostname.size();
    }
    const auto labelLen = labelEnd - labelStart;
    if (labelLen == 0 || labelLen > 63) {
      return false;
    }
    const auto labelFirst = hostname[labelStart];
    const auto labelLast = hostname[labelEnd - 1];
    if ((!util::isAlpha(labelFirst) && !util::isDigit(labelFirst)) ||
        (!util::isAlpha(labelLast) && !util::isDigit(labelLast))) {
      return false;
    }

    for (auto i = labelStart; i < labelEnd; ++i) {
      const auto c = hostname[i];
      if (!util::isAlpha(c) && !util::isDigit(c) && c != '-') {
        return false;
      }
    }

    if (labelEnd == hostname.size()) {
      break;
    }
    labelStart = labelEnd + 1;
  }
  return true;
}

std::string normalizeTLSHost(const std::string& value,
                             const std::string& originalValue,
                             void (*throwBadConfig)(const std::string&))
{
  auto host = util::strip(value);
  if (!isAsyncDnsTLSHost(host)) {
    throwBadConfig(originalValue);
  }
  return util::toLower(std::move(host));
}

std::pair<std::string, std::string>
splitServerAndTLSHost(const std::string& entry, const std::string& value,
                      void (*throwBadConfig)(const std::string&))
{
  auto delim = entry.find('#');
  if (delim == std::string::npos) {
    return std::make_pair(entry, std::string());
  }
  if (entry.find('#', delim + 1) != std::string::npos) {
    throwBadConfig(value);
  }
  auto server = util::strip(entry.substr(0, delim));
  auto tlsHost = normalizeTLSHost(entry.substr(delim + 1), value,
                                  throwBadConfig);
  if (server.empty()) {
    throwBadConfig(value);
  }
  return std::make_pair(std::move(server), std::move(tlsHost));
}

void validateDirectConnectHost(const std::string& scheme,
                               const std::string& host)
{
  if (!util::isNumericHost(host)) {
    throw DL_ABORT_EX(
        fmt("Bad async DNS %s server '%s': direct connect requires a "
            "numeric host",
            scheme.c_str(), host.c_str()));
  }
}
} // namespace

AsyncDnsServerConfig parseAsyncDnsDotServerConfig(const std::string& value)
{
  auto entry = util::strip(value);
  if (entry.empty()) {
    throwBadDotServerConfig(value);
  }
  std::string tlsHost;
  std::tie(entry, tlsHost) =
      splitServerAndTLSHost(entry, value, throwBadDotServerConfig);

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

  return {host, port, tlsHost.empty() ? defaultTLSHost(host) : tlsHost};
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

void validateAsyncDnsDotServerConfigForDirectConnect(
    const std::vector<AsyncDnsServerConfig>& configs)
{
  if (configs.empty()) {
    throw DL_ABORT_EX("No async DNS DoT server configured");
  }
  for (const auto& config : configs) {
    validateDirectConnectHost("DoT", config.connectHost);
  }
}

AsyncDohServerConfig parseAsyncDnsDohServerConfig(const std::string& value)
{
  auto entry = util::strip(value);
  if (entry.empty()) {
    throwBadDohServerConfig(value);
  }

  uri::UriStruct us;
  if (!uri::parse(us, entry) || us.protocol != "https" || us.host.empty()) {
    throwBadDohServerConfig(value);
  }
  if (!us.username.empty() || us.hasPassword) {
    throwBadDohServerConfig(value);
  }
  uri_split_result splitResult;
  if (uri_split(&splitResult, entry.c_str()) != 0 ||
      !(splitResult.field_set & (1 << USR_PATH)) ||
      ((splitResult.field_set & (1 << USR_PORT)) && splitResult.port == 0)) {
    throwBadDohServerConfig(value);
  }
  std::string tlsHost;
  if (splitResult.field_set & (1 << USR_FRAGMENT)) {
    tlsHost = normalizeTLSHost(
        uri::getFieldString(splitResult, USR_FRAGMENT, entry.c_str()), value,
        throwBadDohServerConfig);
  }

  auto path = us.dir + us.file + us.query;
  if (path.empty() || path[0] != '/') {
    throwBadDohServerConfig(value);
  }

  return {us.host, us.port == 0 ? DEFAULT_DOH_PORT : us.port,
          tlsHost.empty() ? defaultTLSHost(us.host) : tlsHost, path};
}

std::vector<AsyncDohServerConfig>
parseAsyncDnsDohServerConfigList(const std::string& value)
{
  std::vector<AsyncDohServerConfig> result;
  if (value.empty()) {
    return result;
  }

  std::vector<std::string> entries;
  util::split(std::begin(value), std::end(value), std::back_inserter(entries),
              ',', true, true);
  for (const auto& entry : entries) {
    result.push_back(parseAsyncDnsDohServerConfig(entry));
  }
  return result;
}

void validateAsyncDnsDohServerConfigForDirectConnect(
    const std::vector<AsyncDohServerConfig>& configs)
{
  if (configs.empty()) {
    throw DL_ABORT_EX("No async DNS DoH server configured");
  }
  for (const auto& config : configs) {
    validateDirectConnectHost("DoH", config.connectHost);
  }
}

} // namespace aria2
