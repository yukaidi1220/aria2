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
#include "Http2ResponseAdapter.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Session.h"
#  include "DlAbortEx.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"
#  include "a2functional.h"
#  include "util.h"

namespace aria2 {

namespace {
bool isHttp2ConnectionSpecificHeader(const std::string& name)
{
  return name == "connection" || name == "keep-alive" ||
         name == "proxy-connection" || name == "transfer-encoding" ||
         name == "upgrade";
}
} // namespace

std::unique_ptr<HttpResponse>
createHttpResponseFromHttp2Event(const Http2ResponseEvent& event)
{
  if (!event.headersComplete) {
    throw DL_ABORT_EX("HTTP/2 response headers are incomplete");
  }
  if (event.status < 100 || event.status > 999) {
    throw DL_ABORT_EX("Malformed HTTP/2 response status");
  }

  auto header = make_unique<HttpHeader>();
  header->setVersion("HTTP/2");
  header->setStatusCode(event.status);

  for (const auto& field : event.headers) {
    if (field.name.empty() || field.name[0] == ':') {
      continue;
    }

    auto name = util::toLower(field.name);
    if (isHttp2ConnectionSpecificHeader(name)) {
      continue;
    }

    auto fieldId = idInterestingHeader(name.c_str());
    if (fieldId != HttpHeader::MAX_INTERESTING_HEADER) {
      header->put(fieldId, util::strip(field.value));
    }
  }

  auto response = make_unique<HttpResponse>();
  response->setHttpHeader(std::move(header));
  return response;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
