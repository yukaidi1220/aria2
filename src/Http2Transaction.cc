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
#include "Http2Transaction.h"

#ifdef HAVE_LIBNGHTTP2

#  include "DlAbortEx.h"
#  include "HttpResponse.h"

namespace aria2 {

Http2Transaction::Http2Transaction() = default;

Http2Transaction::~Http2Transaction() = default;

int32_t Http2Transaction::submitRequest(const Http2HeaderBlock& headers)
{
  if (streamId_ != 0) {
    throw DL_ABORT_EX("HTTP/2 transaction already has an active stream");
  }
  streamId_ = connection_.submitRequest(headers);
  return streamId_;
}

std::string Http2Transaction::drainOutboundData()
{
  return connection_.drainOutboundData();
}

void Http2Transaction::feedInboundData(const std::string& data)
{
  connection_.feedInboundData(data);
}

bool Http2Transaction::hasActiveStream() const { return streamId_ != 0; }

int32_t Http2Transaction::getStreamId() const { return streamId_; }

const Http2ResponseEvent* Http2Transaction::findResponseEvent() const
{
  if (streamId_ == 0) {
    return nullptr;
  }
  return connection_.findResponseEvent(streamId_);
}

std::string Http2Transaction::popResponseBody(size_t maxLen)
{
  if (streamId_ == 0) {
    return std::string();
  }
  return connection_.popResponseBody(streamId_, maxLen);
}

std::unique_ptr<Http2ResponseEvent> Http2Transaction::popResponseEvent()
{
  if (streamId_ == 0) {
    return nullptr;
  }
  auto response = connection_.findResponseEvent(streamId_);
  if (!response || !response->streamClosed) {
    return nullptr;
  }
  auto event = connection_.popResponseEvent(streamId_);
  if (event) {
    streamId_ = 0;
  }
  return event;
}

std::unique_ptr<HttpResponse> Http2Transaction::popHttpResponse()
{
  if (streamId_ == 0) {
    return nullptr;
  }
  auto event = connection_.findResponseEvent(streamId_);
  if (!event || !event->streamClosed) {
    return nullptr;
  }
  auto response = connection_.popHttpResponse(streamId_);
  if (response) {
    streamId_ = 0;
  }
  return response;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
