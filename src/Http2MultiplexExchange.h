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
#ifndef D_HTTP2_MULTIPLEX_EXCHANGE_H
#define D_HTTP2_MULTIPLEX_EXCHANGE_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <cstdint>
#  include <cstddef>
#  include <memory>
#  include <set>
#  include <string>

#  include "Http2Connection.h"
#  include "Http2Transaction.h"
#  include "Http2TransactionPump.h"
#  include "Command.h"

namespace aria2 {

class HttpRequest;
class HttpResponse;
class Http2Transport;

class Http2MultiplexExchange {
private:
  std::unique_ptr<Http2Transport> ownedTransport_;
  Http2Transport& transport_;
  Http2Connection connection_;
  Http2TransactionPump pump_;
  std::set<int32_t> activeStreams_;

public:
  explicit Http2MultiplexExchange(Http2Transport& transport);
  explicit Http2MultiplexExchange(std::unique_ptr<Http2Transport> transport);
  ~Http2MultiplexExchange();

  Http2MultiplexExchange(const Http2MultiplexExchange&) = delete;
  Http2MultiplexExchange&
  operator=(const Http2MultiplexExchange&) = delete;

  int32_t submitRequest(HttpRequest& request);
  int32_t submitRequest(HttpRequest& request, cuid_t cuid);
  int32_t submitRequestAndFlush(HttpRequest& request);
  int32_t submitRequestAndFlush(HttpRequest& request, cuid_t cuid);
  int32_t submitRequest(const Http2HeaderBlock& headers);
  bool flushOutboundData();
  bool readInboundData();
  bool pump();

  bool wantRead() const;
  bool wantWrite() const;
  bool hasBufferedInboundData() const;

  bool hasActiveStreams() const;
  bool hasActiveStream(int32_t streamId) const;
  size_t countActiveStreams() const;
  void cancelStream(int32_t streamId);
  size_t getRemoteMaxConcurrentStreams() const;
  Http2TransactionState getState(int32_t streamId) const;
  std::unique_ptr<HttpResponse> createHttpResponse(int32_t streamId) const;
  std::string popResponseBody(int32_t streamId, size_t maxLen);
  std::unique_ptr<Http2ResponseEvent> popResponseEvent(int32_t streamId);
  std::unique_ptr<HttpResponse> popHttpResponse(int32_t streamId);
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_MULTIPLEX_EXCHANGE_H
