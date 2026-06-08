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
#include "Http2SocketCoreTransport.h"

#ifdef HAVE_LIBNGHTTP2

#  include <utility>

#  include "SocketCore.h"

namespace aria2 {

Http2SocketCoreTransport::Http2SocketCoreTransport(
    std::shared_ptr<SocketCore> socket)
    : socket_(std::move(socket))
{
}

Http2SocketCoreTransport::~Http2SocketCoreTransport() = default;

ssize_t Http2SocketCoreTransport::writeData(const void* data, size_t len)
{
  return socket_->writeData(data, len);
}

ssize_t Http2SocketCoreTransport::readData(void* data, size_t len)
{
  size_t nread = len;
  socket_->readData(data, nread);
  return static_cast<ssize_t>(nread);
}

size_t Http2SocketCoreTransport::getRecvBufferedLength() const
{
  return socket_->getRecvBufferedLength();
}

bool Http2SocketCoreTransport::wantRead() const
{
  return socket_->wantRead();
}

bool Http2SocketCoreTransport::wantWrite() const
{
  return socket_->wantWrite();
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
