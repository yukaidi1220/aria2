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
#include "AsyncNameResolver.h"
#include "AsyncServiceBindingResolver.h"
#include "DlAbortEx.h"
#include "DnsMessage.h"
#include "EventPoll.h"
#include "Exception.h"
#include "HttpHeader.h"
#include "HttpHeaderProcessor.h"
#include "HttpProtocol.h"
#ifdef HAVE_LIBNGHTTP2
#  include "Http2SingleStreamExchange.h"
#  include "Http2Transport.h"
#  include "HttpResponse.h"
#  include <nghttp2/nghttp2.h>
#endif // HAVE_LIBNGHTTP2
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

const char* queryTypeToString(dns::QueryType queryType)
{
  switch (queryType) {
  case dns::TYPE_A:
    return "A";
  case dns::TYPE_AAAA:
    return "AAAA";
  case dns::TYPE_HTTPS:
    return "HTTPS";
  default:
    return "unknown";
  }
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
  std::string result = "https://";
  result += formatHost(server.connectHost);
  result += ":";
  result += util::uitos(server.port);
  result += server.path;
  if (!server.tlsHost.empty() && server.tlsHost != server.connectHost) {
    result += "#";
    result += server.tlsHost;
  }
  return result;
}

std::string formatEndpoint(const std::vector<std::string>& endpoints,
                           size_t index)
{
  if (index >= endpoints.size()) {
    return "-";
  }
  return formatHost(endpoints[index]);
}

const char* dohProtocolToString(HttpProtocol protocol)
{
  switch (protocol) {
  case HTTP_PROTOCOL_HTTP1:
    return "https-h1";
  case HTTP_PROTOCOL_H2:
    return "https-h2";
  case HTTP_PROTOCOL_UNKNOWN:
    return "https-unknown";
  }
  return "https-unknown";
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

std::vector<std::string> createDohAlpnProtocols(bool enableHttp2)
{
  std::vector<std::string> protocols;
#ifdef HAVE_LIBNGHTTP2
  if (enableHttp2) {
    protocols.push_back(HTTP_ALPN_H2);
    protocols.push_back(HTTP_ALPN_HTTP11);
  }
#else  // !HAVE_LIBNGHTTP2
  (void)enableHttp2;
#endif // !HAVE_LIBNGHTTP2
  return protocols;
}

#ifdef HAVE_LIBNGHTTP2
Http2HeaderBlock createDohHttp2Headers(const AsyncDohServerConfig& server,
                                       const std::string& dnsQuery)
{
  Http2HeaderBlock headers;
  headers.emplace_back(":method", "POST");
  headers.emplace_back(":scheme", "https");
  headers.emplace_back(":authority", createHostHeader(server));
  headers.emplace_back(":path", server.path);
  headers.emplace_back("accept", "application/dns-message");
  headers.emplace_back("content-type", "application/dns-message");
  headers.emplace_back("content-length", util::uitos(dnsQuery.size()));
  return headers;
}
#endif // HAVE_LIBNGHTTP2
} // namespace

class AsyncDohExchange {
public:
  virtual ~AsyncDohExchange() = default;

  virtual void reset() = 0;

  virtual void submitRequest(const AsyncDohServerConfig& server,
                             const std::string& dnsQuery) = 0;

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

#ifdef HAVE_LIBNGHTTP2
class AsyncDohHttp2TransportAdapter : public Http2Transport {
public:
  explicit AsyncDohHttp2TransportAdapter(AsyncDohTransport& transport)
      : transport_(transport)
  {
  }

  virtual ssize_t writeData(const void* data, size_t len) CXX11_OVERRIDE
  {
    return transport_.writeData(data, len);
  }

  virtual ssize_t readData(void* data, size_t len) CXX11_OVERRIDE
  {
    return static_cast<ssize_t>(transport_.readData(data, len));
  }

  virtual size_t getRecvBufferedLength() const CXX11_OVERRIDE
  {
    return transport_.getRecvBufferedLength();
  }

  virtual bool wantRead() const CXX11_OVERRIDE
  {
    return transport_.wantRead();
  }

  virtual bool wantWrite() const CXX11_OVERRIDE
  {
    return transport_.wantWrite();
  }

private:
  AsyncDohTransport& transport_;
};

class AsyncDohHttp2Exchange : public AsyncDohExchange {
public:
  virtual void reset() CXX11_OVERRIDE
  {
    server_ = AsyncDohServerConfig();
    dnsQuery_.clear();
    transportAdapter_.reset();
    exchange_.reset();
    responseHeaderChecked_ = false;
    responseBuffer_.clear();
  }

  virtual void submitRequest(const AsyncDohServerConfig& server,
                             const std::string& dnsQuery) CXX11_OVERRIDE
  {
    reset();
    server_ = server;
    dnsQuery_ = dnsQuery;
  }

  virtual bool writeRequest(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    ensureExchange(transport);
    exchange_->flushOutboundData();
    return !exchange_->wantWrite();
  }

  virtual DohExchangeReadResult
  readResponseHeader(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    ensureExchange(transport);
    exchange_->readInboundData();
    exchange_->flushOutboundData();
    collectResponseBody();

    auto state = exchange_->getState();
    if (!state.headersComplete) {
      if (state.streamClosed) {
        throw DL_ABORT_EX("DoH HTTP/2 stream closed before response headers");
      }
      if (state.errorCode != 0) {
        throw DL_ABORT_EX("DoH HTTP/2 stream failed before response headers");
      }
    }
    if (!state.responseAvailable || !state.headersComplete) {
      return DOH_EXCHANGE_READ_HEADER_PENDING;
    }
    checkResponseHeader();
    if (state.streamClosed) {
      checkStreamClosed(state);
      return DOH_EXCHANGE_READ_COMPLETE;
    }
    return DOH_EXCHANGE_READ_BODY_PENDING;
  }

  virtual bool readResponseBody(AsyncDohTransport& transport) CXX11_OVERRIDE
  {
    ensureExchange(transport);
    exchange_->readInboundData();
    exchange_->flushOutboundData();
    collectResponseBody();
    auto state = exchange_->getState();
    if (!state.streamClosed) {
      return false;
    }
    checkStreamClosed(state);
    return true;
  }

  virtual const std::vector<unsigned char>& getResponseBody() const
      CXX11_OVERRIDE
  {
    return responseBuffer_;
  }

  virtual size_t getReadProgress() const CXX11_OVERRIDE
  {
    auto progress = responseBuffer_.size();
    if (responseHeaderChecked_) {
      ++progress;
    }
    if (exchange_) {
      progress += exchange_->getState().bodyLength;
    }
    return progress;
  }

private:
  void ensureExchange(AsyncDohTransport& transport)
  {
    if (exchange_) {
      return;
    }
    transportAdapter_ = make_unique<AsyncDohHttp2TransportAdapter>(transport);
    exchange_ = make_unique<Http2SingleStreamExchange>(*transportAdapter_);
    exchange_->submitRequest(createDohHttp2Headers(server_, dnsQuery_),
                             dnsQuery_);
  }

  void collectResponseBody()
  {
    for (;;) {
      auto chunk = exchange_->popResponseBody(MAX_DOH_MESSAGE_SIZE);
      if (chunk.empty()) {
        return;
      }
      if (responseBuffer_.size() + chunk.size() > MAX_DOH_MESSAGE_SIZE) {
        throw DL_ABORT_EX("DoH HTTP/2 response body is too large");
      }
      responseBuffer_.insert(std::end(responseBuffer_), std::begin(chunk),
                             std::end(chunk));
    }
  }

  void checkResponseHeader()
  {
    if (responseHeaderChecked_) {
      return;
    }
    auto response = exchange_->createHttpResponse();
    if (!response) {
      throw DL_ABORT_EX("DoH HTTP/2 response header was not parsed");
    }
    if (response->getStatusCode() != 200) {
      throw DL_ABORT_EX(
          fmt("DoH server returned HTTP/2 status %d",
              response->getStatusCode()));
    }
    responseHeaderChecked_ = true;
  }

  void checkStreamClosed(const Http2TransactionState& state) const
  {
    if (state.errorCode != NGHTTP2_NO_ERROR) {
      throw DL_ABORT_EX(
          fmt("DoH HTTP/2 stream closed with error %u", state.errorCode));
    }
    if (responseBuffer_.empty()) {
      throw DL_ABORT_EX("DoH HTTP/2 response body is empty");
    }
  }

  AsyncDohServerConfig server_;
  std::string dnsQuery_;
  std::unique_ptr<AsyncDohHttp2TransportAdapter> transportAdapter_;
  std::unique_ptr<Http2SingleStreamExchange> exchange_;
  bool responseHeaderChecked_ = false;
  std::vector<unsigned char> responseBuffer_;
};
#endif // HAVE_LIBNGHTTP2

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
    AsyncDohTransportFactory transportFactory, bool enableHttp2,
    AsyncDohBootstrapResolverFactory bootstrapResolverFactory,
    int bootstrapFamily)
    : family_(family),
      bootstrapFamily_(bootstrapFamily),
      servers_(std::move(servers)),
      enableHttp2_(enableHttp2),
      transportFactory_(std::move(transportFactory)),
      bootstrapResolverFactory_(std::move(bootstrapResolverFactory)),
      exchange_(make_unique<AsyncDohHttp1Exchange>()),
      status_(STATUS_READY),
      state_(DOH_IDLE),
      serverIndex_(0),
      currentEndpointIndex_(0),
      queryType_(getQueryType(family)),
      queryId_(0)
{
  if (!transportFactory_) {
    transportFactory_ = createSocketCoreDohTransport;
  }
  if (!bootstrapResolverFactory_) {
    bootstrapResolverFactory_ = [](int family) {
      return make_unique<AsyncNameResolver>(family, std::string());
    };
  }
}

AsyncDohNameResolver::~AsyncDohNameResolver() = default;

void AsyncDohNameResolver::resolve(const std::string& name)
{
  startResolve(name, name, getQueryType(family_));
}

void AsyncDohNameResolver::resolveHttpsServiceBinding(const std::string& name,
                                                      uint16_t port)
{
  startResolve(name, createHttpsServiceBindingQueryName(name, port),
               dns::TYPE_HTTPS);
}

void AsyncDohNameResolver::startResolve(const std::string& name,
                                        const std::string& queryName,
                                        dns::QueryType queryType)
{
  hostname_ = name;
  queryName_ = queryName;
  queryType_ = queryType;
  resolvedAddresses_.clear();
  serviceBindingRecords_.clear();
  error_.clear();
  socks_.clear();
  bootstrapResolver_.reset();
  transport_.reset();
  serverIndex_ = 0;
  currentEndpoints_.clear();
  currentEndpointIndex_ = 0;
  exchange_->reset();
  status_ = STATUS_QUERYING;
  state_ = DOH_IDLE;

  try {
    queryId_ = nextQueryId();
    dnsQuery_ = dns::createQuery(queryId_, queryName_, queryType_);
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
  return status_ == STATUS_QUERYING &&
         ((bootstrapResolver_ && bootstrapResolver_->usable()) ||
          (transport_ && transport_->getSocket() != badSocket()));
}

bool AsyncDohNameResolver::startCurrentServer()
{
  while (serverIndex_ < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    try {
      exchange_->reset();
      transport_.reset();
      bootstrapResolver_.reset();
      currentEndpoints_.clear();
      currentEndpointIndex_ = 0;
      return prepareCurrentServerEndpoints();
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

bool AsyncDohNameResolver::prepareCurrentServerEndpoints()
{
  const auto& server = servers_[serverIndex_];
  if (!util::isNumericHost(server.connectHost)) {
    return startBootstrapResolver();
  }

  currentEndpoints_.push_back(server.connectHost);
  currentEndpointIndex_ = 0;
  return startCurrentEndpoint();
}

bool AsyncDohNameResolver::startBootstrapResolver()
{
  const auto& server = servers_[serverIndex_];
  bootstrapResolver_ = bootstrapResolverFactory_(bootstrapFamily_);
  if (!bootstrapResolver_) {
    throw DL_ABORT_EX("DoH bootstrap resolver factory returned null");
  }
  bootstrapResolver_->resolve(server.connectHost);
  state_ = DOH_BOOTSTRAP_RESOLVING;
  A2_LOG_NETWORK(fmt("DNS: DoH bootstrap resolving %s for %s %s",
                     server.connectHost.c_str(), queryTypeToString(queryType_),
                     hostname_.c_str()));
  if (bootstrapResolver_->getStatus() == STATUS_SUCCESS) {
    currentEndpoints_ = bootstrapResolver_->getResolvedAddresses();
    bootstrapResolver_.reset();
    currentEndpointIndex_ = 0;
    if (currentEndpoints_.empty()) {
      throw DL_ABORT_EX("DoH bootstrap returned no address");
    }
    return startCurrentEndpoint();
  }
  if (bootstrapResolver_->getStatus() == STATUS_ERROR) {
    auto error = bootstrapResolver_->getError();
    bootstrapResolver_.reset();
    throw DL_ABORT_EX(
        fmt("DoH bootstrap failed for %s: %s", server.connectHost.c_str(),
            error.c_str()));
  }
  updateBootstrapSocketEvents();
  return true;
}

bool AsyncDohNameResolver::startCurrentEndpoint()
{
  const auto& server = servers_[serverIndex_];
  while (currentEndpointIndex_ < currentEndpoints_.size()) {
    const auto& connectHost = currentEndpoints_[currentEndpointIndex_];
    try {
      exchange_->reset();
      transport_ = transportFactory_(server);
      if (!transport_) {
        throw DL_ABORT_EX("DoH transport factory returned null");
      }
      transport_->startConnect(connectHost, server.port);
      state_ = DOH_CONNECTING;
      A2_LOG_NETWORK(fmt("DNS: DoH connecting to %s via %s for %s %s",
                         formatDohServer(server).c_str(),
                         formatHost(connectHost).c_str(),
                         queryTypeToString(queryType_), hostname_.c_str()));
      updateSocketEvents();
      return true;
    }
    catch (Exception& e) {
      error_ = e.what();
      A2_LOG_NETWORK(fmt("DNS: DoH endpoint %s for server %s failed: %s",
                         formatHost(connectHost).c_str(),
                         formatDohServer(server).c_str(), error_.c_str()));
      ++currentEndpointIndex_;
      transport_.reset();
    }
  }
  return failCurrentServerOrRetry(
      error_.empty() ? "DoH server connection failed" : error_);
}

bool AsyncDohNameResolver::failCurrentEndpointOrServer(std::string error)
{
  const auto& server = servers_[serverIndex_];
  if (currentEndpointIndex_ < currentEndpoints_.size()) {
    A2_LOG_NETWORK(fmt("DNS: DoH endpoint %s for server %s failed: %s",
                       formatHost(currentEndpoints_[currentEndpointIndex_])
                           .c_str(),
                       formatDohServer(server).c_str(), error.c_str()));
  }
  if (currentEndpointIndex_ + 1 < currentEndpoints_.size()) {
    ++currentEndpointIndex_;
    transport_.reset();
    state_ = DOH_IDLE;
    return startCurrentEndpoint();
  }
  return failCurrentServerOrRetry(std::move(error));
}

bool AsyncDohNameResolver::failCurrentServerOrRetry(std::string error)
{
  if (serverIndex_ + 1 < servers_.size()) {
    const auto& server = servers_[serverIndex_];
    A2_LOG_NETWORK(fmt("DNS: DoH server %s failed: %s",
                       formatDohServer(server).c_str(), error.c_str()));
    ++serverIndex_;
    bootstrapResolver_.reset();
    transport_.reset();
    currentEndpoints_.clear();
    currentEndpointIndex_ = 0;
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
  bootstrapResolver_.reset();
  transport_.reset();
  A2_LOG_NETWORK(fmt("DNS: DoH %s %s failed: %s",
                     queryTypeToString(queryType_),
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
  if (state_ == DOH_BOOTSTRAP_RESOLVING) {
    updateBootstrapSocketEvents();
    return;
  }
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

void AsyncDohNameResolver::updateBootstrapSocketEvents()
{
  socks_.clear();
  if (!bootstrapResolver_) {
    return;
  }
  const auto& bootstrapSocks = bootstrapResolver_->getsock();
  socks_.insert(std::end(socks_), std::begin(bootstrapSocks),
                std::end(bootstrapSocks));
}

bool AsyncDohNameResolver::eventReady(sock_t readfd, sock_t writefd) const
{
  if (!transport_) {
    return false;
  }
  const auto fd = transport_->getSocket();
  return readfd == fd || writefd == fd;
}

bool AsyncDohNameResolver::bootstrapEventReady(sock_t readfd,
                                               sock_t writefd) const
{
  if (!bootstrapResolver_) {
    return false;
  }
  const auto& bootstrapSocks = bootstrapResolver_->getsock();
  for (const auto& sock : bootstrapSocks) {
    if (readfd == sock.fd || writefd == sock.fd) {
      return true;
    }
  }
  return false;
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
      failCurrentEndpointOrServer(e.what());
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
                            createDohAlpnProtocols(enableHttp2_));
}

void AsyncDohNameResolver::prepareExchangeForSelectedProtocol()
{
  const auto& server = servers_[serverIndex_];
  auto protocol = decideHttpProtocolFromSelectedAlpn(
      transport_->getSelectedAlpnProtocol(), enableHttp2_);
  switch (protocol) {
  case HTTP_PROTOCOL_HTTP1:
    exchange_ = make_unique<AsyncDohHttp1Exchange>();
    break;
  case HTTP_PROTOCOL_H2:
#ifdef HAVE_LIBNGHTTP2
    exchange_ = make_unique<AsyncDohHttp2Exchange>();
    A2_LOG_NETWORK("DNS: DoH using HTTP/2");
    break;
#else  // !HAVE_LIBNGHTTP2
    throw DL_ABORT_EX(
        "DoH HTTP/2 was selected but aria2 was built without nghttp2");
#endif // !HAVE_LIBNGHTTP2
  case HTTP_PROTOCOL_UNKNOWN:
    throw DL_ABORT_EX("Unsupported DoH HTTP protocol");
  }
  exchange_->submitRequest(server, dnsQuery_);
}

void AsyncDohNameResolver::process(sock_t readfd, sock_t writefd)
{
  if (status_ == STATUS_QUERYING && state_ == DOH_BOOTSTRAP_RESOLVING) {
    if (bootstrapEventReady(readfd, writefd) ||
        (readfd == badSocket() && writefd == badSocket())) {
      processBootstrapResolver(readfd, writefd);
      return;
    }
    updateSocketEvents();
    return;
  }

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
    failCurrentEndpointOrServer(e.what());
  }
  updateSocketEvents();
}

void AsyncDohNameResolver::processTimeout()
{
  if (status_ == STATUS_QUERYING && state_ == DOH_BOOTSTRAP_RESOLVING) {
    processBootstrapResolver(badSocket(), badSocket());
    return;
  }
  processBufferedRead();
}

void AsyncDohNameResolver::processBootstrapResolver(sock_t readfd,
                                                    sock_t writefd)
{
  if (!bootstrapResolver_) {
    updateSocketEvents();
    return;
  }
  bootstrapResolver_->process(readfd, writefd);
  if (bootstrapResolver_->getStatus() == STATUS_SUCCESS) {
    const auto& addrs = bootstrapResolver_->getResolvedAddresses();
    currentEndpoints_.assign(std::begin(addrs), std::end(addrs));
    bootstrapResolver_.reset();
    currentEndpointIndex_ = 0;
    if (currentEndpoints_.empty()) {
      failCurrentServerOrRetry("DoH bootstrap returned no address");
      return;
    }
    startCurrentEndpoint();
    return;
  }
  if (bootstrapResolver_->getStatus() == STATUS_ERROR) {
    auto error = fmt("DoH bootstrap failed for %s: %s",
                     servers_[serverIndex_].connectHost.c_str(),
                     bootstrapResolver_->getError().c_str());
    bootstrapResolver_.reset();
    failCurrentServerOrRetry(error);
    return;
  }
  updateSocketEvents();
}

void AsyncDohNameResolver::processConnecting()
{
  auto error = transport_->getSocketError();
  if (!error.empty()) {
    failCurrentEndpointOrServer(
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
  prepareExchangeForSelectedProtocol();
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
  if (queryType_ == dns::TYPE_HTTPS) {
    serviceBindingRecords_ = dns::parseServiceBindingResponse(
        responseBody.data(), responseBody.size(), queryId_, queryName_,
        queryType_);
    status_ = STATUS_SUCCESS;
    state_ = DOH_DONE;
    socks_.clear();
    const auto& server = servers_[serverIndex_];
    auto endpoint = formatEndpoint(currentEndpoints_, currentEndpointIndex_);
    auto protocol =
        httpProtocolFromSelectedAlpn(transport_->getSelectedAlpnProtocol());
    A2_LOG_NETWORK(fmt("DNS: DoH HTTPS RR %s returned %lu record(s) "
                       "qname=%s server=%s endpoint=%s transport=%s",
                       hostname_.c_str(),
                       static_cast<unsigned long>(
                           serviceBindingRecords_.size()),
                       queryName_.c_str(), formatDohServer(server).c_str(),
                       endpoint.c_str(), dohProtocolToString(protocol)));
    return;
  }

  resolvedAddresses_ = dns::parseResponse(responseBody.data(),
                                          responseBody.size(), queryId_,
                                          queryName_, queryType_);
  if (resolvedAddresses_.empty()) {
    throw DL_ABORT_EX("no address returned by DoH server");
  }
  status_ = STATUS_SUCCESS;
  state_ = DOH_DONE;
  socks_.clear();
  if (A2_LOG_NETWORK_ENABLED) {
    auto addrs = strjoin(std::begin(resolvedAddresses_),
                         std::end(resolvedAddresses_), ", ");
    A2_LOG_NETWORK(fmt("DNS: DoH %s %s -> %s",
                       queryTypeToString(queryType_),
                       hostname_.c_str(), addrs.c_str()));
  }
}

} // namespace aria2

#endif // ENABLE_SSL
