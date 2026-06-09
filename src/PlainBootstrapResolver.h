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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_PLAIN_BOOTSTRAP_RESOLVER_H
#define D_PLAIN_BOOTSTRAP_RESOLVER_H

#include "common.h"

#include <memory>
#include <string>
#include <vector>

#include "AsyncResolver.h"

namespace aria2 {

class PlainBootstrapResolver : public AsyncResolver {
private:
  int family_;
  std::vector<std::shared_ptr<AsyncResolver>> resolvers_;
  std::vector<std::string> resolvedAddresses_;
  std::string error_;
  std::string hostname_;
  std::vector<AsyncResolverSocketEntry> socks_;

  void updateSockets();
  void updateResolvedAddresses();
  void updateError();
  bool socketBelongsTo(const AsyncResolver* resolver, sock_t fd) const;

public:
  PlainBootstrapResolver(int family,
                         std::vector<std::shared_ptr<AsyncResolver>> resolvers);

  virtual void resolve(const std::string& name) CXX11_OVERRIDE;

  virtual const std::vector<std::string>& getResolvedAddresses()
      const CXX11_OVERRIDE;

  virtual const std::string& getError() const CXX11_OVERRIDE;

  virtual STATUS getStatus() const CXX11_OVERRIDE;

  virtual bool usable() const CXX11_OVERRIDE;

  virtual int getFamily() const CXX11_OVERRIDE;

  virtual const std::vector<AsyncResolverSocketEntry>& getsock()
      const CXX11_OVERRIDE;

  virtual void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE;

  virtual void processTimeout() CXX11_OVERRIDE;

  virtual const std::string& getHostname() const CXX11_OVERRIDE;
};

} // namespace aria2

#endif // D_PLAIN_BOOTSTRAP_RESOLVER_H
