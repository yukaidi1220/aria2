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
#ifndef D_HTTP2_SINGLE_STREAM_EXCHANGE_H
#define D_HTTP2_SINGLE_STREAM_EXCHANGE_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <cstddef>
#  include <memory>
#  include <string>

#  include "Http2Transaction.h"
#  include "Http2TransactionPump.h"

namespace aria2 {

class HttpRequest;
class HttpResponse;
class Http2Transport;

class Http2SingleStreamExchange {
private:
  Http2Transport& transport_;
  Http2Transaction transaction_;
  Http2TransactionPump pump_;

public:
  explicit Http2SingleStreamExchange(Http2Transport& transport);
  ~Http2SingleStreamExchange();

  Http2SingleStreamExchange(const Http2SingleStreamExchange&) = delete;
  Http2SingleStreamExchange&
  operator=(const Http2SingleStreamExchange&) = delete;

  int32_t submitRequest(HttpRequest& request);
  bool flushOutboundData();
  bool readInboundData();
  bool pump();

  bool wantRead() const;
  bool wantWrite() const;
  bool hasActiveStream() const;
  int32_t getStreamId() const;
  Http2TransactionState getState() const;
  std::unique_ptr<HttpResponse> createHttpResponse() const;
  std::string popResponseBody(size_t maxLen);
  std::unique_ptr<Http2ResponseEvent> popResponseEvent();
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_SINGLE_STREAM_EXCHANGE_H
