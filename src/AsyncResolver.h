/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_ASYNC_RESOLVER_H
#define D_ASYNC_RESOLVER_H

#include "common.h"

#include <string>
#include <vector>

#include "a2netcompat.h"

namespace aria2 {

struct AsyncResolverSocketEntry {
  sock_t fd;
  int events;
};

class AsyncResolver {
public:
  enum STATUS {
    STATUS_READY,
    STATUS_QUERYING,
    STATUS_SUCCESS,
    STATUS_ERROR,
  };

  virtual ~AsyncResolver() = default;

  virtual void resolve(const std::string& name) = 0;

  virtual const std::vector<std::string>& getResolvedAddresses() const = 0;

  virtual const std::string& getError() const = 0;

  virtual STATUS getStatus() const = 0;

  virtual bool usable() const = 0;

  virtual int getFamily() const = 0;

  virtual const std::vector<AsyncResolverSocketEntry>& getsock() const = 0;

  virtual void process(sock_t readfd, sock_t writefd) = 0;

  virtual void processTimeout()
  {
    process(badSocket(), badSocket());
  }

  virtual const std::string& getHostname() const = 0;

  static sock_t badSocket()
  {
#ifdef HAVE_WINSOCK2_H
    return INVALID_SOCKET;
#else  // !HAVE_WINSOCK2_H
    return -1;
#endif // !HAVE_WINSOCK2_H
  }
};

} // namespace aria2

#endif // D_ASYNC_RESOLVER_H
