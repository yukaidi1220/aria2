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
#ifndef D_ASYNC_DOT_NAME_RESOLVER_H
#define D_ASYNC_DOT_NAME_RESOLVER_H

#include "common.h"

#ifdef ENABLE_SSL

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AsyncDnsServerConfig.h"
#include "AsyncResolver.h"
#include "SocketCore.h"

namespace aria2 {

class AsyncDotTransport {
public:
  virtual ~AsyncDotTransport() = default;

  virtual void startConnect(const std::string& host, uint16_t port) = 0;

  virtual sock_t getSocket() const = 0;

  virtual std::string getSocketError() const = 0;

  virtual bool tlsConnect(const TLSHandshakeParams& params) = 0;

  virtual ssize_t writeData(const void* data, size_t len) = 0;

  virtual size_t readData(void* data, size_t len) = 0;

  virtual bool wantRead() const = 0;

  virtual bool wantWrite() const = 0;
};

using AsyncDotTransportFactory =
    std::function<std::unique_ptr<AsyncDotTransport>(
        const AsyncDnsServerConfig& server)>;

class AsyncDotNameResolver : public AsyncResolver {
public:
  enum DotState {
    DOT_IDLE,
    DOT_CONNECTING,
    DOT_TLS_HANDSHAKING,
    DOT_WRITING_QUERY,
    DOT_READING_RESPONSE_LEN,
    DOT_READING_RESPONSE_BODY,
    DOT_DONE,
    DOT_FAILED,
  };

  AsyncDotNameResolver(int family, std::vector<AsyncDnsServerConfig> servers,
                       AsyncDotTransportFactory transportFactory =
                           AsyncDotTransportFactory());

  virtual ~AsyncDotNameResolver();

  virtual void resolve(const std::string& name) CXX11_OVERRIDE;

  virtual const std::vector<std::string>& getResolvedAddresses() const
      CXX11_OVERRIDE
  {
    return resolvedAddresses_;
  }

  virtual const std::string& getError() const CXX11_OVERRIDE { return error_; }

  virtual STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  virtual bool usable() const CXX11_OVERRIDE;

  virtual int getFamily() const CXX11_OVERRIDE { return family_; }

  virtual const std::vector<AsyncResolverSocketEntry>& getsock() const
      CXX11_OVERRIDE
  {
    return socks_;
  }

  virtual void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE;

  virtual void processTimeout() CXX11_OVERRIDE;

  virtual const std::string& getHostname() const CXX11_OVERRIDE
  {
    return hostname_;
  }

  DotState getDotState() const { return state_; }

private:
  bool startCurrentServer();
  bool failCurrentServerOrRetry(std::string error);
  void fail(std::string error);
  void updateSocketEvents();
  int getEventsForWantDirection(int defaultEvent) const;
  bool eventReady(sock_t readfd, sock_t writefd) const;
  TLSHandshakeParams createTLSHandshakeParams() const;
  void processConnecting();
  void processTlsHandshake();
  void processWriteQuery();
  void processReadResponseLength();
  void processReadResponseBody();
  void finishResponse();

  int family_;
  std::vector<AsyncDnsServerConfig> servers_;
  AsyncDotTransportFactory transportFactory_;
  std::unique_ptr<AsyncDotTransport> transport_;
  std::vector<AsyncResolverSocketEntry> socks_;
  STATUS status_;
  DotState state_;
  size_t serverIndex_;
  std::vector<std::string> resolvedAddresses_;
  std::string error_;
  std::string hostname_;
  uint16_t queryId_;
  std::string writeBuffer_;
  size_t writeOffset_;
  unsigned char responseLengthBuffer_[2];
  size_t responseLengthOffset_;
  std::vector<unsigned char> responseBuffer_;
  size_t responseOffset_;
};

} // namespace aria2

#endif // ENABLE_SSL

#endif // D_ASYNC_DOT_NAME_RESOLVER_H
