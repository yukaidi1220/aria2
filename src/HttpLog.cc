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
#include "HttpLog.h"

#include <cstring>
#include <sstream>

#include "Request.h"
#include "SocketCore.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace {

bool isConfidentialHttpHeader(const std::string& line, const char* name)
{
  auto colon = line.find(':');
  return colon != std::string::npos &&
         util::istartsWith(line.begin(), line.begin() + colon, name) &&
         strlen(name) == colon;
}

} // namespace

std::string formatEndpointForLog(const ::Endpoint& endpoint)
{
  if (endpoint.family == AF_INET6) {
    return fmt("[%s]:%u", endpoint.addr.c_str(),
               static_cast<unsigned int>(endpoint.port));
  }
  return fmt("%s:%u", endpoint.addr.c_str(),
             static_cast<unsigned int>(endpoint.port));
}

std::string formatRequestRemoteEndpointForLog(const Request* req)
{
  const auto& connectedAddr = req->getConnectedAddr();
  if (connectedAddr.empty()) {
    return "unavailable";
  }
  if (getNumericAddressFamily(connectedAddr) == AF_INET6) {
    return fmt("[%s]:%u", connectedAddr.c_str(),
               static_cast<unsigned int>(req->getConnectedPort()));
  }
  return fmt("%s:%u", connectedAddr.c_str(),
             static_cast<unsigned int>(req->getConnectedPort()));
}

std::string formatHttpsConnectionEstablishedLog(cuid_t cuid,
                                                const std::string& host,
                                                const std::string& remote)
{
  return fmt("CUID#%" PRId64 " - HTTPS connection to %s established remote=%s",
             cuid, host.c_str(), remote.c_str());
}

std::string formatTlsConnectedLog(const std::string& remote,
                                  const std::string& sni,
                                  const std::string& verify,
                                  const std::string& version,
                                  const std::string& alpn)
{
  return fmt("TLS: connected remote=%s sni=%s verify=%s version=%s alpn=%s",
             remote.c_str(), sni.empty() ? "none" : sni.c_str(),
             verify.empty() ? "none" : verify.c_str(), version.c_str(),
             alpn.empty() ? "none" : alpn.c_str());
}

std::string eraseHttpHeaderConfidentialInfo(const std::string& request)
{
  std::istringstream istr(request);
  std::string result;
  std::string line;
  while (getline(istr, line)) {
    if (isConfidentialHttpHeader(line, "Authorization")) {
      result += "Authorization: <snip>\n";
    }
    else if (isConfidentialHttpHeader(line, "Proxy-Authorization")) {
      result += "Proxy-Authorization: <snip>\n";
    }
    else if (isConfidentialHttpHeader(line, "Cookie")) {
      result += "Cookie: <snip>\n";
    }
    else if (isConfidentialHttpHeader(line, "Set-Cookie")) {
      result += "Set-Cookie: <snip>\n";
    }
    else {
      result += line;
      result += "\n";
    }
  }
  return result;
}

std::string formatHttpRequestHeaderLog(cuid_t cuid, const std::string& protocol,
                                       const std::string& request)
{
  return fmt("HTTP: CUID#%" PRId64 " - Request headers via %s:\n%s", cuid,
             protocol.c_str(), eraseHttpHeaderConfidentialInfo(request).c_str());
}

std::string formatHttpResponseReceivedLog(cuid_t cuid,
                                          const std::string& remote,
                                          const std::string& header)
{
  return fmt("CUID#%" PRId64 " - Response received from %s:\n%s", cuid,
             remote.c_str(), header.c_str());
}

std::string formatHttpResponseStatusLog(cuid_t cuid, int statusCode,
                                        const std::string& remote)
{
  return fmt("HTTP: CUID#%" PRId64 " - Response status: %d remote=%s", cuid,
             statusCode, remote.c_str());
}

} // namespace aria2
