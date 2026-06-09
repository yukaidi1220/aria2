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

#include <algorithm>
#include <cctype>
#include <iterator>
#include <vector>

#include "DlAbortEx.h"
#include "HostMapping.h"
#include "HttpProtocol.h"
#include "Option.h"
#include "Request.h"
#include "TLSSNIHostMapping.h"
#include "base64.h"
#include "prefs.h"

namespace aria2 {

namespace {

bool isBase64Char(unsigned char c)
{
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') ||
         ('0' <= c && c <= '9') || c == '+' || c == '/';
}

std::string normalizeECHConfigBase64(const std::string& value)
{
  std::string normalized;
  for (auto c : value) {
    auto uc = static_cast<unsigned char>(c);
    if (std::isspace(uc)) {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

std::string decodeECHConfigBase64(const std::string& value)
{
  auto encoded = normalizeECHConfigBase64(value);
  if (encoded.empty() || encoded.size() % 4 != 0) {
    throw DL_ABORT_EX("Bad ECHConfigList base64");
  }

  size_t padding = 0;
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto c = static_cast<unsigned char>(encoded[i]);
    if (c == '=') {
      ++padding;
      if (padding > 2 || i < encoded.size() - 2) {
        throw DL_ABORT_EX("Bad ECHConfigList base64");
      }
      continue;
    }
    if (padding || !isBase64Char(c)) {
      throw DL_ABORT_EX("Bad ECHConfigList base64");
    }
  }

  auto decoded = base64::decode(std::begin(encoded), std::end(encoded));
  if (decoded.empty()) {
    throw DL_ABORT_EX("Bad ECHConfigList base64");
  }
  return decoded;
}

TLSECHParams createHttpECHParams(const Option* option,
                                 const TLSSNIHostConfig& sniHostConfig)
{
  TLSECHParams echParams;
  auto enabled = option && option->getAsBool(PREF_ENABLE_ECH);
  auto configured = option && option->defined(PREF_ECH_CONFIG_BASE64);
  if (!enabled && !configured) {
    return echParams;
  }

  if (sniHostConfig.overridden) {
    throw DL_ABORT_EX(
        "TLS ECH cannot be combined with --tls-sni-host override");
  }
  if (!configured) {
    throw DL_ABORT_EX("TLS ECH requires --ech-config-base64");
  }

  echParams.requested = true;
  echParams.required = true;
  echParams.configList =
      decodeECHConfigBase64(option->get(PREF_ECH_CONFIG_BASE64));
  echParams.source = "manual";
  return echParams;
}

std::vector<std::string> applyHttpsServiceBindingAlpn(
    const Request* request, std::vector<std::string> protocols)
{
  if (!request || !request->hasHttpsServiceBindingEndpointInfo()) {
    return protocols;
  }

  const auto& info = request->getHttpsServiceBindingEndpointInfo();
  if (info.defaultAlpnUsed) {
    return protocols;
  }
  if (info.alpn == HTTP_ALPN_HTTP11) {
    protocols.clear();
    protocols.push_back(HTTP_ALPN_HTTP11);
  }
#ifdef HAVE_LIBNGHTTP2
  else if (info.alpn == HTTP_ALPN_H2 &&
           std::find(std::begin(protocols), std::end(protocols),
                     HTTP_ALPN_H2) != std::end(protocols)) {
    protocols.clear();
    protocols.push_back(HTTP_ALPN_H2);
  }
#endif // HAVE_LIBNGHTTP2
  return protocols;
}

} // namespace

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
  auto params = TLSHandshakeParams(
      sniHostConfig.sniHost, verifyHost,
      applyHttpsServiceBindingAlpn(request, createHttpAlpnProtocols(option)),
      sniHostConfig.overridden);
  params.echParams = createHttpECHParams(option, sniHostConfig);
  return params;
}

} // namespace aria2

#endif // ENABLE_SSL
