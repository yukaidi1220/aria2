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
 * version of the file(s).  If you delete this exception statement from
 * all source files in the program, then also delete it here.
 */
/* copyright --> */
#include "Http2HeaderBlock.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "DlAbortEx.h"
#include "util.h"

namespace aria2 {

Http2Header::Http2Header(std::string name, std::string value)
    : name(std::move(name)), value(std::move(value))
{
}

namespace {
struct Http2RequestTarget {
  std::string scheme;
  std::string authority;
  std::string path;
};

std::string stripCR(const std::string& line)
{
  if (!line.empty() && line.back() == '\r') {
    return line.substr(0, line.size() - 1);
  }
  return line;
}

bool isConnectionSpecificHeader(const std::string& name)
{
  return name == "connection" || name == "keep-alive" ||
         name == "proxy-connection" || name == "transfer-encoding" ||
         name == "upgrade";
}

bool isValidHeaderName(const std::string& name)
{
  if (name.empty()) {
    return false;
  }
  for (auto c : name) {
    if (!util::inRFC2616HttpToken(c)) {
      return false;
    }
  }
  return true;
}

std::string findAuthority(const std::vector<Http2Header>& headers)
{
  for (const auto& header : headers) {
    if (header.name == "host") {
      return header.value;
    }
  }
  return "";
}

void collectConnectionHeaderTokens(std::vector<std::string>& tokens,
                                   const std::string& value)
{
  std::vector<std::string> parts;
  util::split(std::begin(value), std::end(value), std::back_inserter(parts),
              ',', true);
  for (auto& part : parts) {
    tokens.push_back(util::toLower(std::move(part)));
  }
}

bool isNamedByConnectionHeader(const std::vector<std::string>& tokens,
                               const std::string& name)
{
  return std::find(std::begin(tokens), std::end(tokens), name) !=
         std::end(tokens);
}

std::string stripUserinfo(const std::string& authority)
{
  auto userinfoEnd = authority.rfind('@');
  if (userinfoEnd == std::string::npos) {
    return authority;
  }
  return authority.substr(userinfoEnd + 1);
}

Http2RequestTarget parseHttp1RequestTarget(const std::string& target,
                                           const std::string& defaultScheme)
{
  Http2RequestTarget result;
  auto schemeEnd = target.find("://");
  if (schemeEnd == std::string::npos) {
    result.scheme = util::toLower(defaultScheme);
    result.path = target.empty() ? "/" : target;
    return result;
  }

  result.scheme = util::toLower(target.substr(0, schemeEnd));
  auto authorityBegin = schemeEnd + 3;
  auto pathBegin = target.find_first_of("/?", authorityBegin);
  if (pathBegin == std::string::npos) {
    result.authority = stripUserinfo(target.substr(authorityBegin));
    result.path = "/";
    return result;
  }

  result.authority =
      stripUserinfo(target.substr(authorityBegin, pathBegin - authorityBegin));
  if (target[pathBegin] == '?') {
    result.path = "/" + target.substr(pathBegin);
  }
  else {
    result.path = target.substr(pathBegin);
  }
  return result;
}
} // namespace

bool http2AllowsRequestHeader(const std::string& name,
                              const std::string& value)
{
  auto lname = util::toLower(name);
  if (lname.empty() || lname[0] == ':' || lname == "host" ||
      isConnectionSpecificHeader(lname)) {
    return false;
  }
  if (lname == "te") {
    return util::strieq(util::strip(value), "trailers");
  }
  return true;
}

Http2HeaderBlock createHttp2HeaderBlockFromHttp1Request(
    const std::string& request, const std::string& scheme)
{
  std::vector<std::string> lines;
  util::split(std::begin(request), std::end(request), std::back_inserter(lines),
              '\n', false, true);
  if (lines.empty()) {
    throw DL_ABORT_EX("HTTP request is empty");
  }

  auto requestLine = stripCR(lines[0]);
  auto methodEnd = requestLine.find(' ');
  if (methodEnd == std::string::npos) {
    throw DL_ABORT_EX("Malformed HTTP request line");
  }
  auto targetBegin = requestLine.find_first_not_of(' ', methodEnd);
  if (targetBegin == std::string::npos) {
    throw DL_ABORT_EX("Malformed HTTP request line");
  }
  auto targetEnd = requestLine.find(' ', targetBegin);
  if (targetEnd == std::string::npos) {
    throw DL_ABORT_EX("Malformed HTTP request line");
  }

  auto method = requestLine.substr(0, methodEnd);
  auto target = requestLine.substr(targetBegin, targetEnd - targetBegin);
  if (method.empty() || target.empty() || scheme.empty() ||
      util::strieq(method, "CONNECT")) {
    throw DL_ABORT_EX("Malformed HTTP request line");
  }

  std::vector<Http2Header> headers;
  headers.reserve(lines.size());
  std::vector<std::string> connectionTokens;

  for (size_t i = 1; i < lines.size(); ++i) {
    auto line = stripCR(lines[i]);
    if (line.empty()) {
      break;
    }

    auto nameEnd = line.find(':');
    if (nameEnd == std::string::npos) {
      throw DL_ABORT_EX("Malformed HTTP header field");
    }

    auto name = util::toLower(line.substr(0, nameEnd));
    if (!isValidHeaderName(name)) {
      throw DL_ABORT_EX("Malformed HTTP header field");
    }
    auto value = util::strip(line.substr(nameEnd + 1));
    if (name == "connection") {
      collectConnectionHeaderTokens(connectionTokens, value);
    }
    headers.emplace_back(std::move(name), std::move(value));
  }

  auto targetFields = parseHttp1RequestTarget(target, scheme);
  if (targetFields.scheme.empty()) {
    throw DL_ABORT_EX("Malformed HTTP request line");
  }
  if (targetFields.authority.empty()) {
    targetFields.authority = findAuthority(headers);
  }
  if (targetFields.authority.empty()) {
    throw DL_ABORT_EX("HTTP request does not include Host header field");
  }

  Http2HeaderBlock result;
  result.reserve(headers.size() + 4);
  result.emplace_back(":method", method);
  result.emplace_back(":scheme", targetFields.scheme);
  result.emplace_back(":authority", targetFields.authority);
  result.emplace_back(":path", targetFields.path);

  for (const auto& header : headers) {
    if (header.name != "host" &&
        !isNamedByConnectionHeader(connectionTokens, header.name) &&
        http2AllowsRequestHeader(header.name, header.value)) {
      if (header.name == "te") {
        result.emplace_back("te", "trailers");
      }
      else {
        result.push_back(header);
      }
    }
  }

  return result;
}

} // namespace aria2
