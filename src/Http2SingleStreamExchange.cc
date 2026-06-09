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
 * file(s) with this exception, you may extend this exception statement from
 * your version, but you are not obligated to do so.  If you do not wish to do
 * so, delete this exception statement from your version.  If you delete this
 * exception statement from all source files in the program, then also delete
 * it here.
 */
/* copyright --> */
#include "Http2SingleStreamExchange.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2HeaderBlock.h"
#  include "HttpResponse.h"
#  include "HttpRequest.h"
#  include "Http2Transport.h"

#  include <utility>

namespace aria2 {

Http2SingleStreamExchange::Http2SingleStreamExchange(
    Http2Transport& transport)
    : ownedTransport_(), transport_(transport), transaction_(),
      pump_(transaction_, transport_)
{
}

Http2SingleStreamExchange::Http2SingleStreamExchange(
    std::unique_ptr<Http2Transport> transport)
    : ownedTransport_(std::move(transport)), transport_(*ownedTransport_),
      transaction_(), pump_(transaction_, transport_)
{
}

Http2SingleStreamExchange::~Http2SingleStreamExchange() = default;

int32_t Http2SingleStreamExchange::submitRequest(HttpRequest& request)
{
  auto streamId = transaction_.submitRequest(
      createHttp2HeaderBlockFromHttpRequest(request));
  pump_.notifyPendingOutboundData();
  return streamId;
}

int32_t Http2SingleStreamExchange::submitRequest(
    const Http2HeaderBlock& headers, const std::string& body)
{
  auto streamId = transaction_.submitRequest(headers, body);
  pump_.notifyPendingOutboundData();
  return streamId;
}

bool Http2SingleStreamExchange::flushOutboundData()
{
  return pump_.flushOutboundData();
}

bool Http2SingleStreamExchange::readInboundData()
{
  return pump_.readInboundData();
}

bool Http2SingleStreamExchange::pump() { return pump_.pump(); }

bool Http2SingleStreamExchange::wantRead() const { return pump_.wantRead(); }

bool Http2SingleStreamExchange::wantWrite() const { return pump_.wantWrite(); }

bool Http2SingleStreamExchange::hasActiveStream() const
{
  return transaction_.hasActiveStream();
}

int32_t Http2SingleStreamExchange::getStreamId() const
{
  return transaction_.getStreamId();
}

Http2TransactionState Http2SingleStreamExchange::getState() const
{
  return transaction_.getState();
}

std::unique_ptr<HttpResponse>
Http2SingleStreamExchange::createHttpResponse() const
{
  return transaction_.createHttpResponse();
}

std::string Http2SingleStreamExchange::popResponseBody(size_t maxLen)
{
  return transaction_.popResponseBody(maxLen);
}

std::unique_ptr<Http2ResponseEvent>
Http2SingleStreamExchange::popResponseEvent()
{
  return transaction_.popResponseEvent();
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
