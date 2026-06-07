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
#include "AsyncDotNameResolver.h"

#ifdef ENABLE_SSL

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "A2STR.h"
#include "DlAbortEx.h"
#include "DnsMessage.h"
#include "EventPoll.h"
#include "Exception.h"
#include "LogFactory.h"
#include "a2functional.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace {
const size_t MAX_DOT_MESSAGE_SIZE = 65535;

uint16_t nextQueryId()
{
  static uint16_t queryId = 0;
  ++queryId;
  if (queryId == 0) {
    ++queryId;
  }
  return queryId;
}

dns::QueryType getQueryType(int family)
{
  return family == AF_INET6 ? dns::TYPE_AAAA : dns::TYPE_A;
}

const char* familyToString(int family)
{
  return family == AF_INET6 ? "AAAA" : "A";
}

std::string formatDotServer(const AsyncDnsServerConfig& server)
{
  std::string result;
  if (server.connectHost.find(':') != std::string::npos &&
      server.connectHost.find(']') == std::string::npos) {
    result += "[";
    result += server.connectHost;
    result += "]";
  }
  else {
    result += server.connectHost;
  }
  result += ":";
  result += util::uitos(server.port);
  if (!server.tlsHost.empty() && server.tlsHost != server.connectHost) {
    result += " (TLS ";
    result += server.tlsHost;
    result += ")";
  }
  return result;
}

class SocketCoreDotTransport : public AsyncDotTransport {
public:
  SocketCoreDotTransport() : socket_(std::make_shared<SocketCore>()) {}

  virtual void startConnect(const std::string& host, uint16_t port)
      CXX11_OVERRIDE
  {
    socket_->establishConnection(host, port);
  }

  virtual sock_t getSocket() const CXX11_OVERRIDE
  {
    return socket_->getSockfd();
  }

  virtual std::string getSocketError() const CXX11_OVERRIDE
  {
    return socket_->getSocketError();
  }

  virtual bool tlsConnect(const TLSHandshakeParams& params) CXX11_OVERRIDE
  {
    return socket_->tlsConnect(params);
  }

  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE
  {
    return socket_->writeData(data, len);
  }

  virtual size_t readData(void* data, size_t len) CXX11_OVERRIDE
  {
    socket_->readData(data, len);
    return len;
  }

  virtual size_t getRecvBufferedLength() const CXX11_OVERRIDE
  {
    return socket_->getRecvBufferedLength();
  }

  virtual bool wantRead() const CXX11_OVERRIDE { return socket_->wantRead(); }

  virtual bool wantWrite() const CXX11_OVERRIDE
  {
    return socket_->wantWrite();
  }

private:
  std::shared_ptr<SocketCore> socket_;
};

std::unique_ptr<AsyncDotTransport>
createSocketCoreDotTransport(const AsyncDnsServerConfig&)
{
  return make_unique<SocketCoreDotTransport>();
}
} // namespace

AsyncDotNameResolver::AsyncDotNameResolver(
    int family, std::vector<AsyncDnsServerConfig> servers,
    AsyncDotTransportFactory transportFactory)
    : family_(family),
      servers_(std::move(servers)),
      transportFactory_(std::move(transportFactory)),
      status_(STATUS_READY),
      state_(DOT_IDLE),
      serverIndex_(0),
      queryId_(0),
      writeOffset_(0),
      responseLengthBuffer_{0, 0},
      responseLengthOffset_(0),
      responseOffset_(0)
{
  if (!transportFactory_) {
    transportFactory_ = createSocketCoreDotTransport;
  }
}

AsyncDotNameResolver::~AsyncDotNameResolver() = default;

void AsyncDotNameResolver::resolve(const std::string& name)
{
  hostname_ = name;
  resolvedAddresses_.clear();
  error_.clear();
  socks_.clear();
  transport_.reset();
  serverIndex_ = 0;
  writeOffset_ = 0;
  responseLengthOffset_ = 0;
  responseOffset_ = 0;
  responseBuffer_.clear();
  status_ = STATUS_QUERYING;
  state_ = DOT_IDLE;

  try {
    queryId_ = nextQueryId();
    auto query = dns::createQuery(queryId_, hostname_, getQueryType(family_));
    if (query.size() > MAX_DOT_MESSAGE_SIZE) {
      fail("DNS query is too large for DoT");
      return;
    }
    writeBuffer_.clear();
    writeBuffer_.reserve(query.size() + 2);
    writeBuffer_.push_back(static_cast<char>((query.size() >> 8) & 0xff));
    writeBuffer_.push_back(static_cast<char>(query.size() & 0xff));
    writeBuffer_.append(query);
  }
  catch (Exception& e) {
    fail(e.what());
    return;
  }

  if (servers_.empty()) {
    fail("no DoT server configured");
    return;
  }

  startCurrentServer();
}

bool AsyncDotNameResolver::usable() const
{
  return status_ == STATUS_QUERYING && transport_ &&
         transport_->getSocket() != badSocket();
}

bool AsyncDotNameResolver::startCurrentServer()
{
  while (serverIndex_ < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    try {
      writeOffset_ = 0;
      responseLengthOffset_ = 0;
      responseOffset_ = 0;
      responseBuffer_.clear();
      transport_ = transportFactory_(server);
      if (!transport_) {
        throw DL_ABORT_EX("DoT transport factory returned null");
      }
      transport_->startConnect(server.connectHost, server.port);
      state_ = DOT_CONNECTING;
      A2_LOG_NETWORK(fmt("DNS: DoT connecting to %s for %s %s",
                         formatDotServer(server).c_str(),
                         familyToString(family_), hostname_.c_str()));
      updateSocketEvents();
      return true;
    }
    catch (Exception& e) {
      error_ = e.what();
      A2_LOG_NETWORK(fmt("DNS: DoT server %s failed: %s",
                         formatDotServer(server).c_str(), error_.c_str()));
      ++serverIndex_;
    }
  }

  fail(error_.empty() ? "DoT server connection failed" : error_);
  return false;
}

bool AsyncDotNameResolver::failCurrentServerOrRetry(std::string error)
{
  if (serverIndex_ + 1 < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    A2_LOG_NETWORK(fmt("DNS: DoT server %s failed: %s",
                       formatDotServer(server).c_str(), error.c_str()));
    ++serverIndex_;
    transport_.reset();
    state_ = DOT_IDLE;
    return startCurrentServer();
  }

  fail(std::move(error));
  return false;
}

void AsyncDotNameResolver::fail(std::string error)
{
  error_ = std::move(error);
  status_ = STATUS_ERROR;
  state_ = DOT_FAILED;
  socks_.clear();
  transport_.reset();
  A2_LOG_NETWORK(fmt("DNS: DoT %s %s failed: %s", familyToString(family_),
                     hostname_.empty() ? "(pending)" : hostname_.c_str(),
                     error_.c_str()));
}

int AsyncDotNameResolver::getEventsForWantDirection(int defaultEvent) const
{
  int events = 0;
  if (transport_->wantRead()) {
    events |= EventPoll::EVENT_READ;
  }
  if (transport_->wantWrite()) {
    events |= EventPoll::EVENT_WRITE;
  }
  if (!events) {
    events = defaultEvent;
  }
  return events;
}

void AsyncDotNameResolver::updateSocketEvents()
{
  socks_.clear();
  if (!transport_) {
    return;
  }

  auto fd = transport_->getSocket();
  if (fd == badSocket()) {
    return;
  }

  int events = 0;
  switch (state_) {
  case DOT_CONNECTING:
    events = EventPoll::EVENT_WRITE;
    break;
  case DOT_TLS_HANDSHAKING:
  case DOT_WRITING_QUERY:
    events = getEventsForWantDirection(EventPoll::EVENT_WRITE);
    break;
  case DOT_READING_RESPONSE_LEN:
  case DOT_READING_RESPONSE_BODY:
    events = getEventsForWantDirection(EventPoll::EVENT_READ);
    break;
  default:
    break;
  }

  if (events) {
    socks_.push_back(AsyncResolverSocketEntry{fd, events});
  }
}

bool AsyncDotNameResolver::eventReady(sock_t readfd, sock_t writefd) const
{
  if (!transport_) {
    return false;
  }
  const auto fd = transport_->getSocket();
  return readfd == fd || writefd == fd;
}

bool AsyncDotNameResolver::canProcessBufferedRead() const
{
  return transport_ &&
         (state_ == DOT_READING_RESPONSE_LEN ||
          state_ == DOT_READING_RESPONSE_BODY) &&
         transport_->getRecvBufferedLength() > 0;
}

void AsyncDotNameResolver::processBufferedRead()
{
  while (status_ == STATUS_QUERYING && canProcessBufferedRead()) {
    try {
      auto prevState = state_;
      auto prevLengthOffset = responseLengthOffset_;
      auto prevResponseOffset = responseOffset_;
      if (state_ == DOT_READING_RESPONSE_LEN) {
        processReadResponseLength();
      }
      else if (state_ == DOT_READING_RESPONSE_BODY) {
        processReadResponseBody();
      }
      else {
        break;
      }

      if (state_ == prevState && responseLengthOffset_ == prevLengthOffset &&
          responseOffset_ == prevResponseOffset) {
        break;
      }
    }
    catch (Exception& e) {
      failCurrentServerOrRetry(e.what());
      break;
    }
  }
  updateSocketEvents();
}

TLSHandshakeParams AsyncDotNameResolver::createTLSHandshakeParams() const
{
  const auto& server = servers_[serverIndex_];
  const auto verifyHost =
      server.tlsHost.empty() ? server.connectHost : server.tlsHost;
  return TLSHandshakeParams(verifyHost, verifyHost);
}

void AsyncDotNameResolver::process(sock_t readfd, sock_t writefd)
{
  if (status_ != STATUS_QUERYING || !transport_ ||
      !eventReady(readfd, writefd)) {
    processBufferedRead();
    updateSocketEvents();
    return;
  }

  try {
    switch (state_) {
    case DOT_CONNECTING:
      processConnecting();
      break;
    case DOT_TLS_HANDSHAKING:
      processTlsHandshake();
      break;
    case DOT_WRITING_QUERY:
      processWriteQuery();
      break;
    case DOT_READING_RESPONSE_LEN:
      processReadResponseLength();
      break;
    case DOT_READING_RESPONSE_BODY:
      processReadResponseBody();
      break;
    default:
      break;
    }
  }
  catch (Exception& e) {
    failCurrentServerOrRetry(e.what());
  }
  updateSocketEvents();
}

void AsyncDotNameResolver::processTimeout()
{
  processBufferedRead();
}

void AsyncDotNameResolver::processConnecting()
{
  auto error = transport_->getSocketError();
  if (!error.empty()) {
    failCurrentServerOrRetry(
        fmt("DoT connection failed: %s", error.c_str()));
    return;
  }
  state_ = DOT_TLS_HANDSHAKING;
}

void AsyncDotNameResolver::processTlsHandshake()
{
  if (!transport_->tlsConnect(createTLSHandshakeParams())) {
    return;
  }
  state_ = DOT_WRITING_QUERY;
  writeOffset_ = 0;
}

void AsyncDotNameResolver::processWriteQuery()
{
  auto nwrite =
      transport_->writeData(writeBuffer_.data() + writeOffset_,
                            writeBuffer_.size() - writeOffset_);
  if (nwrite < 0) {
    throw DL_ABORT_EX("DoT query write failed");
  }
  if (nwrite == 0) {
    if (!transport_->wantRead() && !transport_->wantWrite()) {
      throw DL_ABORT_EX("DoT connection closed while writing query");
    }
    return;
  }
  writeOffset_ += static_cast<size_t>(nwrite);
  if (writeOffset_ == writeBuffer_.size()) {
    state_ = DOT_READING_RESPONSE_LEN;
    responseLengthOffset_ = 0;
  }
}

void AsyncDotNameResolver::processReadResponseLength()
{
  auto nread =
      transport_->readData(responseLengthBuffer_ + responseLengthOffset_,
                           sizeof(responseLengthBuffer_) -
                               responseLengthOffset_);
  if (nread == 0) {
    if (!transport_->wantRead() && !transport_->wantWrite()) {
      throw DL_ABORT_EX("DoT connection closed while reading response length");
    }
    return;
  }
  responseLengthOffset_ += nread;
  if (responseLengthOffset_ < sizeof(responseLengthBuffer_)) {
    return;
  }

  auto responseLength =
      (static_cast<size_t>(responseLengthBuffer_[0]) << 8) |
      static_cast<size_t>(responseLengthBuffer_[1]);
  if (responseLength == 0 || responseLength > MAX_DOT_MESSAGE_SIZE) {
    throw DL_ABORT_EX(fmt("Bad DoT response length %lu",
                          static_cast<unsigned long>(responseLength)));
  }

  responseBuffer_.assign(responseLength, 0);
  responseOffset_ = 0;
  state_ = DOT_READING_RESPONSE_BODY;
}

void AsyncDotNameResolver::processReadResponseBody()
{
  auto nread = transport_->readData(responseBuffer_.data() + responseOffset_,
                                    responseBuffer_.size() - responseOffset_);
  if (nread == 0) {
    if (!transport_->wantRead() && !transport_->wantWrite()) {
      throw DL_ABORT_EX("DoT connection closed while reading response body");
    }
    return;
  }
  responseOffset_ += nread;
  if (responseOffset_ == responseBuffer_.size()) {
    finishResponse();
  }
}

void AsyncDotNameResolver::finishResponse()
{
  resolvedAddresses_ =
      dns::parseResponse(responseBuffer_.data(), responseBuffer_.size(),
                         queryId_, hostname_, getQueryType(family_));
  if (resolvedAddresses_.empty()) {
    throw DL_ABORT_EX("no address returned by DoT server");
  }
  status_ = STATUS_SUCCESS;
  state_ = DOT_DONE;
  socks_.clear();
  if (A2_LOG_NETWORK_ENABLED) {
    auto addrs = strjoin(std::begin(resolvedAddresses_),
                         std::end(resolvedAddresses_), ", ");
    A2_LOG_NETWORK(fmt("DNS: DoT %s %s -> %s", familyToString(family_),
                       hostname_.c_str(), addrs.c_str()));
  }
}

} // namespace aria2

#endif // ENABLE_SSL
