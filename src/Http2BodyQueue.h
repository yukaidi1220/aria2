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
#ifndef D_HTTP2_BODY_QUEUE_H
#define D_HTTP2_BODY_QUEUE_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <deque>
#  include <cstdint>
#  include <string>

namespace aria2 {

class Http2BodyQueue {
private:
  std::deque<std::string> chunks_;
  size_t size_;
  size_t capacity_;
  bool closed_;
  uint32_t errorCode_;

public:
  explicit Http2BodyQueue(size_t capacity = 2 * 1024 * 1024);
  ~Http2BodyQueue();

  Http2BodyQueue(const Http2BodyQueue&) = default;
  Http2BodyQueue& operator=(const Http2BodyQueue&) = default;
  Http2BodyQueue(Http2BodyQueue&&) = default;
  Http2BodyQueue& operator=(Http2BodyQueue&&) = default;

  bool push(const unsigned char* data, size_t len);
  std::string pop(size_t maxLen);
  std::string drainAll();
  void clear();
  void close(uint32_t errorCode);

  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  size_t available() const { return capacity_ - size_; }
  bool closed() const { return closed_; }
  uint32_t errorCode() const { return errorCode_; }
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_BODY_QUEUE_H
