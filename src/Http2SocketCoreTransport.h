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
#ifndef D_HTTP2_SOCKET_CORE_TRANSPORT_H
#define D_HTTP2_SOCKET_CORE_TRANSPORT_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <memory>

#  include "Http2Transport.h"

namespace aria2 {

class SocketCore;

class Http2SocketCoreTransport : public Http2Transport {
private:
  std::shared_ptr<SocketCore> socket_;

public:
  explicit Http2SocketCoreTransport(std::shared_ptr<SocketCore> socket);
  ~Http2SocketCoreTransport();

  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE;

  virtual ssize_t readData(void* data, size_t len) CXX11_OVERRIDE;

  virtual size_t getRecvBufferedLength() const CXX11_OVERRIDE;

  virtual bool wantRead() const CXX11_OVERRIDE;

  virtual bool wantWrite() const CXX11_OVERRIDE;

  const std::shared_ptr<SocketCore>& getSocket() const { return socket_; }
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_SOCKET_CORE_TRANSPORT_H
