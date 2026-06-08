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
#include "HttpProtocol.h"

#include "DlAbortEx.h"
#include "fmt.h"

namespace aria2 {

const char HTTP_ALPN_H2[] = "h2";
const char HTTP_ALPN_HTTP11[] = "http/1.1";

HttpProtocol httpProtocolFromSelectedAlpn(const std::string& selectedAlpn)
{
  if (selectedAlpn.empty() || selectedAlpn == HTTP_ALPN_HTTP11) {
    return HTTP_PROTOCOL_HTTP1;
  }
  if (selectedAlpn == HTTP_ALPN_H2) {
    return HTTP_PROTOCOL_H2;
  }
  return HTTP_PROTOCOL_UNKNOWN;
}

HttpProtocol requireSupportedHttpProtocolFromSelectedAlpn(
    const std::string& selectedAlpn, bool allowHttp2)
{
  auto protocol = httpProtocolFromSelectedAlpn(selectedAlpn);
  if (protocol == HTTP_PROTOCOL_UNKNOWN) {
    throw DL_ABORT_EX(
        fmt("Unsupported HTTP ALPN protocol '%s'", selectedAlpn.c_str()));
  }
  if (protocol == HTTP_PROTOCOL_H2 && !allowHttp2) {
    throw DL_ABORT_EX(
        "HTTP/2 was selected by ALPN but --enable-http2=false");
  }
  return protocol;
}

HttpProtocol decideHttpProtocolFromSelectedAlpn(const std::string& selectedAlpn,
                                                bool enableHttp2)
{
  switch (requireSupportedHttpProtocolFromSelectedAlpn(selectedAlpn,
                                                       enableHttp2)) {
  case HTTP_PROTOCOL_HTTP1:
    return HTTP_PROTOCOL_HTTP1;
  case HTTP_PROTOCOL_H2:
    throw DL_ABORT_EX(
        "HTTP/2 was selected by ALPN but the download path is not implemented "
        "yet");
  case HTTP_PROTOCOL_UNKNOWN:
    break;
  }
  return HTTP_PROTOCOL_UNKNOWN;
}

void validateHttpSelectedAlpnProtocol(const std::string& selectedAlpn)
{
  decideHttpProtocolFromSelectedAlpn(selectedAlpn, false);
}

} // namespace aria2
