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
#include "AsyncDohNameResolver.h"

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
#include "HttpHeader.h"
#include "HttpHeaderProcessor.h"
#include "LogFactory.h"
#include "a2functional.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

enum DohExchangeReadResult {
  DOH_EXCHANGE_READ_HEADER_PENDING,
  DOH_EXCHANGE_READ_BODY_PENDING,
  DOH_EXCHANGE_READ_COMPLETE,
};

namespace {
const size_t MAX_DOH_MESSAGE_SIZE = 65535;
const size_t MAX_DOH_READ_SIZE = 16_k;

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

std::string formatHost(const std::string& host)
{
  if (host.find(':') != std::string::npos &&
      host.find(']') == std::string::npos) {
    std::string result = "[";
    result += host;
    result += "]";
    return result;
  }
  return host;
}

std::string formatDohServer(const AsyncDohServerConfig& server)
{
  auto result = formatHost(server.connectHost);
  result += ":";
  result += util::uitos(server.port);
  result += server.path;
  if (!server.tlsHost.empty() && server.tlsHost != server.connectHost) {
    result += " (TLS ";
    result += server.tlsHost;
    result += ")";
  }
  return result;
}

std::string createHostHeader(const AsyncDohServerConfig& server)
{
  const auto& host =
      server.tlsHost.empty() ? server.connectHost : server.tlsHost;
  auto result = formatHost(host);
  if (server.port != 443) {
    result += ":";
    result += util::uitos(server.port);
  }
  return result;
}

std::string createDohRequest(const AsyncDohServerConfig& server,
                             const std::string& dnsQuery)
{
  std::string request;
  request += "POST ";
  request += server.path;
  request += " HTTP/1.1\r\n";
  request += "Host: ";
  request += createHostHeader(server);
  request += "\r\n";
  request += "Accept: application/dns-message\r\n";
  request += "Content-Type: application/dns-message\r\n";
  request += "Content-Length: ";
  request += util::uitos(dnsQuery.size());
  request += "\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";
  request += dnsQuery;
  return request;
}
} // namespace

class AsyncDohExchange {
public:
  virtual ~AsyncDohExchange() = default;

  virtual void reset() = 0;

  virtual void submitRequest(const AsyncDohServerConfig& server,
                             const std::string& dnsQuery) = 0;

  virtual std::vector<std::string> createAlpnProtocols() const = 0;

  virtual bool writeRequest(AsyncDohTransport& transport) = 0;

  virtual DohExchangeReadResult
  readResponseHeader(AsyncDohTransport& transport) = 0;

  virtual bool readResponseBody(AsyncDohTransport& transport) = 0;

  virtual const std::vector<unsigned char>& getResponseBody() const = 0;

  virtual size_t getReadProgress() const = 0;
};

class AsyncDohHttp1Exchange : public AsyncDohExchange {
public:
  AsyncDohHttp1Exchange()
      : httpHeaderProcessor_(HttpHeaderProcessor::CLIENT_PARSER),
        writeOffset_(0),
        responseOffset_(0)
  {
  }

  virtual void reset() CXX11_OVERRIDE
  {
    writeBuffer_.clear();
    writeOffset_ = 0;
    httpHeaderProcessor_.clear();
    responseHeader_.reset();
    responseBuffer_.clear();
    responseOffset_ = 0;
  }

  virtual void submitRequest(const AsyncDohServerConfig& server,
                             const std::string& dnsQuery) CXX11_OVERRIDE
  {
    reset();
    writeBuffer_ = createDohRequest(server, dnsQuery);
  }

  virtual std::vector<std::string> createAlpnProtocols() const CXX11_OVERRIDE
  {
    return std::vector<std::string>();
  }

  virtual bool writeRequest(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    auto nwrite = transport.writeData(writeBuffer_.data() + writeOffset_,
                                      writeBuffer_.size() - writeOffset_);
    if (nwrite < 0) {
      throw DL_ABORT_EX("DoH request write failed");
    }
    if (nwrite == 0) {
      if (!transport.wantRead() && !transport.wantWrite()) {
        throw DL_ABORT_EX("DoH connection closed while writing request");
      }
      return false;
    }
    writeOffset_ += static_cast<size_t>(nwrite);
    return writeOffset_ == writeBuffer_.size();
  }

  virtual DohExchangeReadResult
  readResponseHeader(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    unsigned char buf[MAX_DOH_READ_SIZE];
    auto nread = transport.readData(buf, sizeof(buf));
    if (nread == 0) {
      if (!transport.wantRead() && !transport.wantWrite()) {
        throw DL_ABORT_EX(
            "DoH connection closed while reading response header");
      }
      return DOH_EXCHANGE_READ_HEADER_PENDING;
    }

    if (!httpHeaderProcessor_.parse(buf, nread)) {
      return DOH_EXCHANGE_READ_HEADER_PENDING;
    }

    auto bodyOffset = httpHeaderProcessor_.getLastBytesProcessed();
    responseHeader_ = httpHeaderProcessor_.getResult();
    prepareResponseBody();
    if (bodyOffset < nread) {
      appendResponseBody(buf + bodyOffset, nread - bodyOffset);
    }
    return responseComplete() ? DOH_EXCHANGE_READ_COMPLETE
                              : DOH_EXCHANGE_READ_BODY_PENDING;
  }

  virtual bool readResponseBody(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    if (responseComplete()) {
      return true;
    }

    auto nread = transport.readData(responseBuffer_.data() + responseOffset_,
                                    responseBuffer_.size() - responseOffset_);
    if (nread == 0) {
      if (!transport.wantRead() && !transport.wantWrite()) {
        throw DL_ABORT_EX(
            "DoH connection closed while reading response body");
      }
      return false;
    }
    responseOffset_ += nread;
    return responseComplete();
  }

  bool responseComplete() const
  {
    return !responseBuffer_.empty() && responseOffset_ == responseBuffer_.size();
  }

  virtual const std::vector<unsigned char>& getResponseBody() const
      CXX11_OVERRIDE
  {
    return responseBuffer_;
  }

  virtual size_t getReadProgress() const CXX11_OVERRIDE
  {
    return httpHeaderProcessor_.getHeaderString().size() + responseOffset_;
  }

private:
  void prepareResponseBody()
  {
    if (!responseHeader_) {
      throw DL_ABORT_EX("DoH response header was not parsed");
    }
    if (responseHeader_->getStatusCode() != 200) {
      throw DL_ABORT_EX(
          fmt("DoH server returned HTTP status %d",
              responseHeader_->getStatusCode()));
    }
    if (responseHeader_->defined(HttpHeader::TRANSFER_ENCODING)) {
      throw DL_ABORT_EX("DoH Transfer-Encoding response is not supported");
    }
    const auto& contentLength =
        responseHeader_->find(HttpHeader::CONTENT_LENGTH);
    int64_t n;
    if (!util::parseLLIntNoThrow(n, contentLength) || n <= 0 ||
        n > static_cast<int64_t>(MAX_DOH_MESSAGE_SIZE)) {
      throw DL_ABORT_EX("Bad DoH response Content-Length");
    }
    responseBuffer_.assign(static_cast<size_t>(n), 0);
    responseOffset_ = 0;
  }

  void appendResponseBody(const unsigned char* data, size_t len)
  {
    if (responseOffset_ + len > responseBuffer_.size()) {
      throw DL_ABORT_EX("DoH response body is larger than Content-Length");
    }
    memcpy(responseBuffer_.data() + responseOffset_, data, len);
    responseOffset_ += len;
  }

  std::string writeBuffer_;
  size_t writeOffset_;
  HttpHeaderProcessor httpHeaderProcessor_;
  std::unique_ptr<HttpHeader> responseHeader_;
  std::vector<unsigned char> responseBuffer_;
  size_t responseOffset_;
};

namespace {
class SocketCoreDohTransport : public AsyncDohTransport {
public:
  SocketCoreDohTransport() : socket_(std::make_shared<SocketCore>()) {}

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

  virtual std::string getSelectedAlpnProtocol() const CXX11_OVERRIDE
  {
    return socket_->getSelectedAlpnProtocol();
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

std::unique_ptr<AsyncDohTransport>
createSocketCoreDohTransport(const AsyncDohServerConfig&)
{
  return make_unique<SocketCoreDohTransport>();
}
} // namespace

AsyncDohNameResolver::AsyncDohNameResolver(
    int family, std::vector<AsyncDohServerConfig> servers,
    AsyncDohTransportFactory transportFactory)
    : family_(family),
      servers_(std::move(servers)),
      transportFactory_(std::move(transportFactory)),
      exchange_(make_unique<AsyncDohHttp1Exchange>()),
      status_(STATUS_READY),
      state_(DOH_IDLE),
      serverIndex_(0),
      queryId_(0)
{
  if (!transportFactory_) {
    transportFactory_ = createSocketCoreDohTransport;
  }
}

AsyncDohNameResolver::~AsyncDohNameResolver() = default;

void AsyncDohNameResolver::resolve(const std::string& name)
{
  hostname_ = name;
  resolvedAddresses_.clear();
  error_.clear();
  socks_.clear();
  transport_.reset();
  serverIndex_ = 0;
  exchange_->reset();
  status_ = STATUS_QUERYING;
  state_ = DOH_IDLE;

  try {
    queryId_ = nextQueryId();
    dnsQuery_ = dns::createQuery(queryId_, hostname_, getQueryType(family_));
    if (dnsQuery_.size() > MAX_DOH_MESSAGE_SIZE) {
      fail("DNS query is too large for DoH");
      return;
    }
  }
  catch (Exception& e) {
    fail(e.what());
    return;
  }

  if (servers_.empty()) {
    fail("no DoH server configured");
    return;
  }

  startCurrentServer();
}

bool AsyncDohNameResolver::usable() const
{
  return status_ == STATUS_QUERYING && transport_ &&
         transport_->getSocket() != badSocket();
}

bool AsyncDohNameResolver::startCurrentServer()
{
  while (serverIndex_ < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    try {
      exchange_->submitRequest(server, dnsQuery_);
      transport_ = transportFactory_(server);
      if (!transport_) {
        throw DL_ABORT_EX("DoH transport factory returned null");
      }
      transport_->startConnect(server.connectHost, server.port);
      state_ = DOH_CONNECTING;
      A2_LOG_NETWORK(fmt("DNS: DoH connecting to %s for %s %s",
                         formatDohServer(server).c_str(),
                         familyToString(family_), hostname_.c_str()));
      updateSocketEvents();
      return true;
    }
    catch (Exception& e) {
      error_ = e.what();
      A2_LOG_NETWORK(fmt("DNS: DoH server %s failed: %s",
                         formatDohServer(server).c_str(), error_.c_str()));
      ++serverIndex_;
    }
  }

  fail(error_.empty() ? "DoH server connection failed" : error_);
  return false;
}

bool AsyncDohNameResolver::failCurrentServerOrRetry(std::string error)
{
  if (serverIndex_ + 1 < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    A2_LOG_NETWORK(fmt("DNS: DoH server %s failed: %s",
                       formatDohServer(server).c_str(), error.c_str()));
    ++serverIndex_;
    transport_.reset();
    state_ = DOH_IDLE;
    return startCurrentServer();
  }

  fail(std::move(error));
  return false;
}

void AsyncDohNameResolver::fail(std::string error)
{
  error_ = std::move(error);
  status_ = STATUS_ERROR;
  state_ = DOH_FAILED;
  socks_.clear();
  transport_.reset();
  A2_LOG_NETWORK(fmt("DNS: DoH %s %s failed: %s", familyToString(family_),
                     hostname_.empty() ? "(pending)" : hostname_.c_str(),
                     error_.c_str()));
}

int AsyncDohNameResolver::getEventsForWantDirection(int defaultEvent) const
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

void AsyncDohNameResolver::updateSocketEvents()
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
  case DOH_CONNECTING:
    events = EventPoll::EVENT_WRITE;
    break;
  case DOH_TLS_HANDSHAKING:
  case DOH_WRITING_REQUEST:
    events = getEventsForWantDirection(EventPoll::EVENT_WRITE);
    break;
  case DOH_READING_RESPONSE_HEADER:
  case DOH_READING_RESPONSE_BODY:
    events = getEventsForWantDirection(EventPoll::EVENT_READ);
    break;
  default:
    break;
  }

  if (events) {
    socks_.push_back(AsyncResolverSocketEntry{fd, events});
  }
}

bool AsyncDohNameResolver::eventReady(sock_t readfd, sock_t writefd) const
{
  if (!transport_) {
    return false;
  }
  const auto fd = transport_->getSocket();
  return readfd == fd || writefd == fd;
}

bool AsyncDohNameResolver::canProcessBufferedRead() const
{
  return transport_ &&
         (state_ == DOH_READING_RESPONSE_HEADER ||
          state_ == DOH_READING_RESPONSE_BODY) &&
         transport_->getRecvBufferedLength() > 0;
}

void AsyncDohNameResolver::processBufferedRead()
{
  while (status_ == STATUS_QUERYING && canProcessBufferedRead()) {
    try {
      auto prevState = state_;
      auto prevReadProgress = exchange_->getReadProgress();
      auto prevBufferedLength = transport_->getRecvBufferedLength();
      if (state_ == DOH_READING_RESPONSE_HEADER) {
        processReadResponseHeader();
      }
      else if (state_ == DOH_READING_RESPONSE_BODY) {
        processReadResponseBody();
      }
      else {
        break;
      }

      if (state_ == prevState &&
          exchange_->getReadProgress() == prevReadProgress &&
          transport_ &&
          transport_->getRecvBufferedLength() == prevBufferedLength) {
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

TLSHandshakeParams AsyncDohNameResolver::createTLSHandshakeParams() const
{
  const auto& server = servers_[serverIndex_];
  const auto verifyHost =
      server.tlsHost.empty() ? server.connectHost : server.tlsHost;
  return TLSHandshakeParams(verifyHost, verifyHost,
                            exchange_->createAlpnProtocols());
}

void AsyncDohNameResolver::process(sock_t readfd, sock_t writefd)
{
  if (status_ != STATUS_QUERYING || !transport_ ||
      !eventReady(readfd, writefd)) {
    processBufferedRead();
    updateSocketEvents();
    return;
  }

  try {
    switch (state_) {
    case DOH_CONNECTING:
      processConnecting();
      break;
    case DOH_TLS_HANDSHAKING:
      processTlsHandshake();
      break;
    case DOH_WRITING_REQUEST:
      processWriteRequest();
      break;
    case DOH_READING_RESPONSE_HEADER:
      processReadResponseHeader();
      break;
    case DOH_READING_RESPONSE_BODY:
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

void AsyncDohNameResolver::processTimeout()
{
  processBufferedRead();
}

void AsyncDohNameResolver::processConnecting()
{
  auto error = transport_->getSocketError();
  if (!error.empty()) {
    failCurrentServerOrRetry(
        fmt("DoH connection failed: %s", error.c_str()));
    return;
  }
  state_ = DOH_TLS_HANDSHAKING;
}

void AsyncDohNameResolver::processTlsHandshake()
{
  if (!transport_->tlsConnect(createTLSHandshakeParams())) {
    return;
  }
  state_ = DOH_WRITING_REQUEST;
}

void AsyncDohNameResolver::processWriteRequest()
{
  if (exchange_->writeRequest(*transport_)) {
    state_ = DOH_READING_RESPONSE_HEADER;
  }
}

void AsyncDohNameResolver::processReadResponseHeader()
{
  switch (exchange_->readResponseHeader(*transport_)) {
  case DOH_EXCHANGE_READ_HEADER_PENDING:
    return;
  case DOH_EXCHANGE_READ_BODY_PENDING:
    state_ = DOH_READING_RESPONSE_BODY;
    return;
  case DOH_EXCHANGE_READ_COMPLETE:
    finishResponse();
    return;
  }
  throw DL_ABORT_EX("Invalid DoH exchange read state");
}

void AsyncDohNameResolver::processReadResponseBody()
{
  if (exchange_->readResponseBody(*transport_)) {
    finishResponse();
  }
}

void AsyncDohNameResolver::finishResponse()
{
  const auto& responseBody = exchange_->getResponseBody();
  resolvedAddresses_ =
      dns::parseResponse(responseBody.data(), responseBody.size(),
                         queryId_, hostname_, getQueryType(family_));
  if (resolvedAddresses_.empty()) {
    throw DL_ABORT_EX("no address returned by DoH server");
  }
  status_ = STATUS_SUCCESS;
  state_ = DOH_DONE;
  socks_.clear();
  if (A2_LOG_NETWORK_ENABLED) {
    auto addrs = strjoin(std::begin(resolvedAddresses_),
                         std::end(resolvedAddresses_), ", ");
    A2_LOG_NETWORK(fmt("DNS: DoH %s %s -> %s", familyToString(family_),
                       hostname_.c_str(), addrs.c_str()));
  }
}

} // namespace aria2

#endif // ENABLE_SSL
