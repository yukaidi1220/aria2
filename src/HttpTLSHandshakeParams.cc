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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "HttpTLSHandshakeParams.h"

#ifdef ENABLE_SSL

#include <vector>

#include "HostMapping.h"
#include "HttpProtocol.h"
#include "Option.h"
#include "Request.h"
#include "TLSSNIHostMapping.h"
#include "prefs.h"

namespace aria2 {

std::vector<std::string> createHttpAlpnProtocols(const Option* option)
{
  std::vector<std::string> protocols;
#ifdef HAVE_LIBNGHTTP2
  if (option && option->getAsBool(PREF_ENABLE_HTTP2) &&
      !option->getAsBool(PREF_ENABLE_HTTP_PIPELINING)) {
    protocols.push_back(HTTP_ALPN_H2);
    protocols.push_back(HTTP_ALPN_HTTP11);
  }
#else  // !HAVE_LIBNGHTTP2
  (void)option;
#endif // !HAVE_LIBNGHTTP2
  return protocols;
}

TLSHandshakeParams createHttpTLSHandshakeParams(const Request* request,
                                                const Option* option)
{
  const auto verifyHost = getLogicalHostForRequest(request->getHost(), option);
  auto sniHostConfig =
      getTLSSNIHostConfig(request->getHost(), verifyHost, option);
  return TLSHandshakeParams(sniHostConfig.sniHost, verifyHost,
                            createHttpAlpnProtocols(option),
                            sniHostConfig.overridden);
}

} // namespace aria2

#endif // ENABLE_SSL
