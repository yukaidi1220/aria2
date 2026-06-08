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
#include "Http2MultiplexExchange.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2HeaderBlock.h"
#  include "Http2Transport.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"

#  include <utility>

namespace aria2 {

Http2MultiplexExchange::Http2MultiplexExchange(Http2Transport& transport)
    : ownedTransport_(), transport_(transport), connection_(),
      pump_(connection_, transport_)
{
}

Http2MultiplexExchange::Http2MultiplexExchange(
    std::unique_ptr<Http2Transport> transport)
    : ownedTransport_(std::move(transport)), transport_(*ownedTransport_),
      connection_(), pump_(connection_, transport_)
{
}

Http2MultiplexExchange::~Http2MultiplexExchange() = default;

int32_t Http2MultiplexExchange::submitRequest(HttpRequest& request)
{
  return submitRequest(createHttp2HeaderBlockFromHttpRequest(request));
}

int32_t Http2MultiplexExchange::submitRequest(const Http2HeaderBlock& headers)
{
  auto streamId = connection_.submitRequest(headers);
  activeStreams_.insert(streamId);
  pump_.notifyPendingOutboundData();
  return streamId;
}

bool Http2MultiplexExchange::flushOutboundData()
{
  return pump_.flushOutboundData();
}

bool Http2MultiplexExchange::readInboundData()
{
  return pump_.readInboundData();
}

bool Http2MultiplexExchange::pump() { return pump_.pump(); }

bool Http2MultiplexExchange::wantRead() const { return pump_.wantRead(); }

bool Http2MultiplexExchange::wantWrite() const { return pump_.wantWrite(); }

bool Http2MultiplexExchange::hasActiveStreams() const
{
  return !activeStreams_.empty();
}

bool Http2MultiplexExchange::hasActiveStream(int32_t streamId) const
{
  return activeStreams_.find(streamId) != activeStreams_.end();
}

size_t Http2MultiplexExchange::countActiveStreams() const
{
  return activeStreams_.size();
}

size_t Http2MultiplexExchange::getRemoteMaxConcurrentStreams() const
{
  return connection_.getRemoteMaxConcurrentStreams();
}

Http2TransactionState
Http2MultiplexExchange::getState(int32_t streamId) const
{
  Http2TransactionState state;
  if (!hasActiveStream(streamId)) {
    return state;
  }
  state.active = true;

  auto event = connection_.findResponseEvent(streamId);
  if (!event) {
    return state;
  }
  state.responseAvailable = true;
  state.headersComplete = event->headersComplete;
  state.streamClosed = event->streamClosed;
  state.bodyLength = event->body.size();
  state.errorCode = event->errorCode;
  return state;
}

std::unique_ptr<HttpResponse>
Http2MultiplexExchange::createHttpResponse(int32_t streamId) const
{
  if (!hasActiveStream(streamId)) {
    return nullptr;
  }
  return connection_.createHttpResponse(streamId);
}

std::string Http2MultiplexExchange::popResponseBody(int32_t streamId,
                                                    size_t maxLen)
{
  if (!hasActiveStream(streamId)) {
    return std::string();
  }
  return connection_.popResponseBody(streamId, maxLen);
}

std::unique_ptr<Http2ResponseEvent>
Http2MultiplexExchange::popResponseEvent(int32_t streamId)
{
  if (!hasActiveStream(streamId)) {
    return nullptr;
  }
  auto response = connection_.findResponseEvent(streamId);
  if (!response || !response->streamClosed) {
    return nullptr;
  }
  auto event = connection_.popResponseEvent(streamId);
  if (event) {
    activeStreams_.erase(streamId);
  }
  return event;
}

std::unique_ptr<HttpResponse>
Http2MultiplexExchange::popHttpResponse(int32_t streamId)
{
  if (!hasActiveStream(streamId)) {
    return nullptr;
  }
  auto response = connection_.popHttpResponse(streamId);
  if (response) {
    activeStreams_.erase(streamId);
  }
  return response;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
