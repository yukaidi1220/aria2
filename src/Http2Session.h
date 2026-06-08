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
#ifndef D_HTTP2_SESSION_H
#define D_HTTP2_SESSION_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <cstddef>
#  include <cstdint>
#  include <memory>
#  include <string>

#  include "Http2BodyQueue.h"
#  include "Http2HeaderBlock.h"

namespace aria2 {

struct Http2ResponseEvent {
  Http2ResponseEvent() = default;
  ~Http2ResponseEvent() = default;
  Http2ResponseEvent(const Http2ResponseEvent&) = default;
  Http2ResponseEvent& operator=(const Http2ResponseEvent&) = default;
  Http2ResponseEvent(Http2ResponseEvent&&) = default;
  Http2ResponseEvent& operator=(Http2ResponseEvent&&) = default;

  int32_t streamId = 0;
  int status = 0;
  Http2HeaderBlock headers;
  Http2BodyQueue body;
  bool headersComplete = false;
  bool streamClosed = false;
  uint32_t errorCode = 0;
};

class Http2Session {
private:
  struct Impl;
  Impl* impl_;

public:
  Http2Session();
  ~Http2Session();

  Http2Session(const Http2Session&) = delete;
  Http2Session& operator=(const Http2Session&) = delete;

  int32_t submitRequestHeaders(const Http2HeaderBlock& headers);
  std::string drainOutboundData();
  void feedInboundData(const std::string& data);
  size_t getRemoteMaxConcurrentStreams() const;
  bool hasResponseEvent(int32_t streamId) const;
  const Http2ResponseEvent* findResponseEvent(int32_t streamId) const;
  std::string popResponseBody(int32_t streamId, size_t maxLen);
  std::unique_ptr<Http2ResponseEvent> popResponseEvent(int32_t streamId);
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_SESSION_H
