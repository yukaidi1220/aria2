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
 * version, but you are not obligated to do so.  If you do not wish to do so,
 * delete this exception statement from your version.  If you delete this
 * exception statement from all source files in the program, then also delete
 * it here.
 */
/* copyright --> */
#ifndef D_ASYNC_SERVICE_BINDING_RESOLVER_H
#define D_ASYNC_SERVICE_BINDING_RESOLVER_H

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

#include <ares.h>

#include "AsyncResolver.h"
#include "DnsMessage.h"

namespace aria2 {

std::string createHttpsServiceBindingQueryName(const std::string& hostname,
                                               uint16_t port);

class AsyncServiceBindingResolver : public AsyncResolver {
  friend void serviceBindingCallback(void* arg, int status, int timeouts,
                                     unsigned char* abuf, int alen);

private:
  std::vector<AsyncResolverSocketEntry> socks_;
  STATUS status_;
  ares_channel channel_;

  std::vector<dns::ServiceBindingRecord> serviceBindingRecords_;
  std::vector<std::string> resolvedAddresses_;
  std::string error_;
  std::string hostname_;
  std::string queryName_;
  uint16_t port_;
  uint16_t queryId_;

public:
  explicit AsyncServiceBindingResolver(const std::string& servers = "",
                                       bool useTcp = false);

  virtual ~AsyncServiceBindingResolver();

  void resolve(const std::string& name, uint16_t port);

  virtual void resolve(const std::string& name) CXX11_OVERRIDE;

  const std::vector<dns::ServiceBindingRecord>& getServiceBindingRecords() const
  {
    return serviceBindingRecords_;
  }

  virtual const std::vector<std::string>& getResolvedAddresses() const
      CXX11_OVERRIDE
  {
    return resolvedAddresses_;
  }

  virtual const std::string& getError() const CXX11_OVERRIDE
  {
    return error_;
  }

  virtual STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  virtual bool usable() const CXX11_OVERRIDE { return channel_; }

  virtual int getFamily() const CXX11_OVERRIDE { return AF_UNSPEC; }

  virtual const std::vector<AsyncResolverSocketEntry>& getsock() const
      CXX11_OVERRIDE;

  virtual void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE;

  virtual const std::string& getHostname() const CXX11_OVERRIDE
  {
    return hostname_;
  }

  const std::string& getQueryName() const { return queryName_; }

  uint16_t getPort() const { return port_; }

  void handle_sock_state(ares_socket_t sock, int read, int write);
};

} // namespace aria2

#endif // D_ASYNC_SERVICE_BINDING_RESOLVER_H
