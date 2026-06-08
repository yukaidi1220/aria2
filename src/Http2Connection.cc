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
#include "Http2Connection.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2ResponseAdapter.h"
#  include "HttpResponse.h"

namespace aria2 {

Http2Connection::Http2Connection() = default;

Http2Connection::~Http2Connection() = default;

int32_t Http2Connection::submitRequest(const Http2HeaderBlock& headers)
{
  return session_.submitRequestHeaders(headers);
}

std::string Http2Connection::drainOutboundData()
{
  return session_.drainOutboundData();
}

void Http2Connection::feedInboundData(const std::string& data)
{
  session_.feedInboundData(data);
}

bool Http2Connection::hasResponseEvent(int32_t streamId) const
{
  return session_.hasResponseEvent(streamId);
}

const Http2ResponseEvent*
Http2Connection::findResponseEvent(int32_t streamId) const
{
  return session_.findResponseEvent(streamId);
}

std::string Http2Connection::popResponseBody(int32_t streamId, size_t maxLen)
{
  return session_.popResponseBody(streamId, maxLen);
}

std::unique_ptr<Http2ResponseEvent>
Http2Connection::popResponseEvent(int32_t streamId)
{
  return session_.popResponseEvent(streamId);
}

std::unique_ptr<HttpResponse> Http2Connection::popHttpResponse(int32_t streamId)
{
  auto event = session_.findResponseEvent(streamId);
  if (!event || !event->streamClosed) {
    return nullptr;
  }
  auto response = createHttpResponseFromHttp2Event(*event);
  session_.popResponseEvent(streamId);
  return response;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
