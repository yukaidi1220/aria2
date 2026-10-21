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
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "HttpRequestCommand.h"

#include <algorithm>
#include <iterator>

#include "Request.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "FileEntry.h"
#include "HttpResponseCommand.h"
#ifdef HAVE_LIBNGHTTP2
#  include "Http2ConnectionContext.h"
#  include "Http2ResponseCommand.h"
#  include "Http2MultiplexExchange.h"
#  include "Http2SocketCoreTransport.h"
#endif // HAVE_LIBNGHTTP2
#include "HttpConnection.h"
#include "HttpLog.h"
#include "HttpRequest.h"
#include "HttpRequestFactory.h"
#include "SegmentMan.h"
#include "Segment.h"
#include "Option.h"
#include "SocketCore.h"
#include "HostMapping.h"
#include "HttpProtocol.h"
#include "HttpTLSHandshakeParams.h"
#include "ServiceBindingSelector.h"
#include "prefs.h"
#include "a2functional.h"
#include "util.h"
#include "CookieStorage.h"
#include "AuthConfigFactory.h"
#include "AuthConfig.h"
#include "DownloadContext.h"
#include "PieceStorage.h"
#include "DefaultBtProgressInfoFile.h"
#include "Logger.h"
#include "LogFactory.h"
#include "fmt.h"
#include "SocketRecvBuffer.h"
#include "InitiateConnectionCommandFactory.h"
#include "RecoverableException.h"
#include "DlAbortEx.h"

namespace aria2 {

HttpRequestCommand::HttpRequestCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    const std::shared_ptr<HttpConnection>& httpConnection, DownloadEngine* e,
    const std::shared_ptr<SocketCore>& s,
    bool retryTLSHandshakeWithNextAddress)
    : AbstractCommand(cuid, req, fileEntry, requestGroup, e, s,
                      httpConnection->getSocketRecvBuffer()),
      httpConnection_(httpConnection),
      retryTLSHandshakeWithNextAddress_(retryTLSHandshakeWithNextAddress)
{
  setTimeout(std::chrono::seconds(getOption()->getAsInt(PREF_CONNECT_TIMEOUT)));
  disableReadCheckSocket();
  setWriteCheckSocket(getSocket());
}

HttpRequestCommand::~HttpRequestCommand() = default;

#ifdef ENABLE_SSL
namespace {
constexpr uint32_t HTTPS_SERVICE_BINDING_ENDPOINT_FAILURE_TTL = 60;

bool isTLSHandshakeFailure(const RecoverableException& e)
{
  // Only SocketCore::tlsConnect() failures carry this prefix. Other TLS
  // errors (for example "TLS ECH was not accepted") are configuration
  // level failures: they intentionally do not go through address failover
  // and are reported by the download abort path instead.
  return util::startsWith(std::string(e.what()), "SSL/TLS handshake failure:");
}

bool markHttpsServiceBindingEndpointFailed(
    const std::shared_ptr<Request>& req, DownloadEngine* e,
    const std::string& connectedHostname, uint16_t connectedPort)
{
  if (!req->hasHttpsServiceBindingEndpointInfo()) {
    return false;
  }

  const auto& info = req->getHttpsServiceBindingEndpointInfo();
  if (info.connectHost != connectedHostname ||
      info.connectPort != connectedPort || !info.serviceBindingUsed()) {
    return false;
  }

  dns::ServiceBindingEndpoint endpoint;
  endpoint.originHost = info.originHost;
  endpoint.originPort = info.originPort;
  endpoint.connectHost = info.connectHost;
  endpoint.connectPort = info.connectPort;
  endpoint.alpn = info.alpn;
  e->markHttpsServiceBindingEndpointFailed(
      endpoint, HTTPS_SERVICE_BINDING_ENDPOINT_FAILURE_TTL);
  A2_LOG_NETWORK(fmt("HTTPS RR: temporarily avoiding failed endpoint %s:%u "
                     "for origin %s:%u",
                     endpoint.connectHost.c_str(), endpoint.connectPort,
                     endpoint.originHost.c_str(), endpoint.originPort));
  req->clearHttpsServiceBindingEndpointInfo();
  return true;
}

const char* addressFamilyName(int family)
{
  if (family == AF_INET) {
    return "IPv4";
  }
  if (family == AF_INET6) {
    return "IPv6";
  }
  return "unknown";
}

bool tryRetryTLSHandshakeWithNextAddress(
    const std::shared_ptr<Request>& req,
    const std::shared_ptr<Option>& option, DownloadEngine* e, cuid_t cuid,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup)
{
  if (req->getProtocol() != "https" || req->getConnectedAddr().empty()) {
    A2_LOG_NETWORK(fmt("CUID#%" PRId64
                       " - TLS handshake failed; not retrying with another "
                       "address: no connected address to fail over from",
                       cuid));
    return false;
  }
  auto tlsParams = createHttpTLSHandshakeParams(req.get(), option.get());
  if (tlsParams.sniHost != tlsParams.verifyHost) {
    A2_LOG_NETWORK(fmt("CUID#%" PRId64
                       " - TLS handshake failed for %s:%u; not retrying with "
                       "another address: SNI override changes the verification "
                       "host",
                       cuid, req->getConnectedAddr().c_str(),
                       static_cast<unsigned int>(req->getConnectedPort())));
    return false;
  }

  const auto& connectedAddr = req->getConnectedAddr();
  const auto& connectedHostname = req->getConnectedHostname();
  const auto connectedPort = req->getConnectedPort();
  auto failedFamily = getNumericAddressFamily(connectedAddr);
  auto oppositeFamily = failedFamily == AF_INET ? AF_INET6 : AF_INET;
  auto verifyPeer = option->getAsBool(PREF_CHECK_CERTIFICATE);

  // Mark the failed address as bad before querying the cache again:
  // findCachedIPAddress() only returns good addresses, so the queries below
  // must not see the address that just failed.
  e->markBadIPAddress(cuid, connectedHostname, connectedAddr, connectedPort);

  // For IPv6, only a global unicast address counts as usable: scoped
  // addresses (ULA, link-local, ...) are deliberately not used as download
  // targets, so an opposite family which only has scoped IPv6 addresses is
  // treated as unavailable and the request aborts with the TLS error.
  auto hasOppositeFamilyAddress = [&]() {
    if (oppositeFamily != AF_INET6) {
      return !e->findCachedIPAddress(connectedHostname, connectedPort,
                                     oppositeFamily)
                  .empty();
    }
    std::vector<std::string> v6Addrs;
    e->findAllCachedIPAddresses(std::back_inserter(v6Addrs), connectedHostname,
                                connectedPort, AF_INET6);
    return std::find_if(std::begin(v6Addrs), std::end(v6Addrs),
                        [](const std::string& addr) {
                          return isIPv6GlobalUnicastAddress(addr);
                        }) != std::end(v6Addrs);
  }();
  auto oppositeFamilyPenalized =
      fileEntry && fileEntry->isAddressFamilyPenalized(connectedHostname,
                                                       connectedPort,
                                                       oppositeFamily);
  auto hasAnyOtherAddress =
      !e->findCachedIPAddress(connectedHostname, connectedPort).empty();

  auto failover = decideTlsAddressFailover(
      failedFamily, verifyPeer, hasOppositeFamilyAddress,
      oppositeFamilyPenalized, hasAnyOtherAddress);

  if (failover == TLS_ADDRESS_FAILOVER_ABORT) {
    if (!hasAnyOtherAddress &&
        getMappedAddresses(connectedHostname, option.get()).empty()) {
      e->removeCachedIPAddress(connectedHostname, connectedPort);
      markHttpsServiceBindingEndpointFailed(req, e, connectedHostname,
                                            connectedPort);
    }
    std::string reason;
    if (!verifyPeer) {
      reason = "no other resolved address left to try";
    }
    else if (failedFamily != AF_INET && failedFamily != AF_INET6) {
      reason = "failed address family is not selectable";
    }
    else if (!hasOppositeFamilyAddress) {
      reason = fmt("no usable address of the other address family (%s)",
                   addressFamilyName(oppositeFamily));
    }
    else {
      reason = fmt("the other address family (%s) also failed recently",
                   addressFamilyName(oppositeFamily));
    }
    A2_LOG_NETWORK(fmt("CUID#%" PRId64
                       " - TLS handshake failed for %s:%u (family=%s); not "
                       "retrying with another address: %s",
                       cuid, connectedAddr.c_str(),
                       static_cast<unsigned int>(connectedPort),
                       addressFamilyName(failedFamily), reason.c_str()));
    return false;
  }

  if (fileEntry && (failedFamily == AF_INET || failedFamily == AF_INET6)) {
    // Penalize the failed address family so the next address selection
    // prefers the other family.
    fileEntry->recordAddressFamilyFailure(connectedHostname, connectedPort,
                                          failedFamily);
  }

  if (failover == TLS_ADDRESS_FAILOVER_OTHER_FAMILY) {
    A2_LOG_NETWORK(fmt("CUID#%" PRId64
                       " - TLS handshake failed for %s:%u (family=%s), "
                       "retrying with the other address family (%s)",
                       cuid, connectedAddr.c_str(),
                       static_cast<unsigned int>(connectedPort),
                       addressFamilyName(failedFamily),
                       addressFamilyName(oppositeFamily)));
  }
  else {
    A2_LOG_NETWORK(fmt("CUID#%" PRId64
                       " - TLS handshake failed for %s:%u (family=%s), "
                       "retrying another resolved address",
                       cuid, connectedAddr.c_str(),
                       static_cast<unsigned int>(connectedPort),
                       addressFamilyName(failedFamily)));
  }
  e->setNoWait(true);
  e->addCommand(
      InitiateConnectionCommandFactory::createInitiateConnectionCommand(
          cuid, req, fileEntry, requestGroup, e));
  return true;
}
} // namespace
#endif // ENABLE_SSL

bool HttpRequestCommand::executeInternal()
{
  // socket->setBlockingMode();
  HttpProtocol httpProtocol = HTTP_PROTOCOL_HTTP1;
  if (httpConnection_->sendBufferIsEmpty()) {
#ifdef ENABLE_SSL
    if (getRequest()->getProtocol() == "https") {
      auto tlsParams =
          createHttpTLSHandshakeParams(getRequest().get(), getOption().get());
      try {
        if (!getSocket()->tlsConnect(tlsParams)) {
          setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
          setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
          addCommandSelf();
          return false;
        }
      }
      catch (RecoverableException& e) {
        if (isTLSHandshakeFailure(e)) {
          if (retryTLSHandshakeWithNextAddress_ && !proxyRequest_ &&
              getOption()->getAsBool(PREF_TLS_HANDSHAKE_FAILOVER)) {
            if (tryRetryTLSHandshakeWithNextAddress(
                    getRequest(), getOption(), getDownloadEngine(), getCuid(),
                    getFileEntry(), getRequestGroup())) {
              return true;
            }
            // tryRetryTLSHandshakeWithNextAddress() already logged why no
            // address failover was possible.
          }
          else {
            std::string reason;
            if (!retryTLSHandshakeWithNextAddress_) {
              reason = "address failover is not available for this request";
            }
            else if (proxyRequest_) {
              reason = "proxy request";
            }
            else {
              reason = "disabled by --tls-handshake-failover=false";
            }
            auto addrForLog = getRequest()->getConnectedAddr().empty()
                                  ? getRequest()->getHost()
                                  : getRequest()->getConnectedAddr();
            A2_LOG_NETWORK(fmt("CUID#%" PRId64
                               " - TLS handshake failed for %s:%u; not retrying "
                               "with another address: %s",
                               getCuid(), addrForLog.c_str(),
                               static_cast<unsigned int>(
                                   getRequest()->getConnectedPort()),
                               reason.c_str()));
          }
        }
        throw;
      }
      httpProtocol = decideHttpProtocolFromSelectedAlpn(
          getSocket()->getSelectedAlpnProtocol(),
          getOption()->getAsBool(PREF_ENABLE_HTTP2));
      A2_LOG_NETWORK(formatHttpsConnectionEstablishedLog(
          getCuid(), getRequest()->getHost(),
          formatRequestRemoteEndpointForLog(getRequest().get())));
    }
#endif // ENABLE_SSL
#ifdef HAVE_LIBNGHTTP2
    if (httpProtocol == HTTP_PROTOCOL_H2) {
      if (!canSubmitSingleHttp2Stream(getSegments().size())) {
        A2_LOG_INFO(
            "HTTP/2 single-stream download does not support pipelined "
            "segments. Retrying with one segment.");
        return prepareForRetry(0);
      }

      std::unique_ptr<HttpRequest> httpRequest;
      if (getSegments().empty()) {
        httpRequest = createHttpRequest(
            getRequest(), getFileEntry(), std::shared_ptr<Segment>(),
            getOption(), getRequestGroup(), getDownloadEngine(), proxyRequest_);
        setConditionalGetHeader(httpRequest.get(), getRequest(), getFileEntry(),
                                getOption());
      }
      else {
        httpRequest = createHttpRequestForSegment(
            getRequest(), getFileEntry(), getRequestGroup(),
            getDownloadEngine(), getOption(), proxyRequest_, getPieceStorage(),
            getSegments().front());
      }

      auto exchange = std::make_shared<Http2MultiplexExchange>(
          make_unique<Http2SocketCoreTransport>(getSocket()));
      auto requestGroup =
          getDownloadEngine()->getRequestGroupMan()->findGroup(
              getRequestGroup()->getGID());
      if (!requestGroup) {
        throw DL_ABORT_EX("HTTP/2 request group was not registered");
      }
      auto connectionContext = std::make_shared<Http2ConnectionContext>(
          requestGroup, exchange, getSocket(), proxyRequest_ != nullptr);
      auto streamId = exchange->submitRequest(*httpRequest, getCuid());
      getDownloadEngine()->registerActiveHttp2Connection(getRequest().get(),
                                                          connectionContext);
      exchange->flushOutboundData();
      getDownloadEngine()->addCommand(make_unique<Http2ResponseCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(), exchange,
          streamId, std::move(httpRequest), getDownloadEngine(), getSocket(),
          false, connectionContext));
      return true;
    }
#endif // HAVE_LIBNGHTTP2
    if (getSegments().empty()) {
      auto httpRequest = createHttpRequest(
          getRequest(), getFileEntry(), std::shared_ptr<Segment>(), getOption(),
          getRequestGroup(), getDownloadEngine(), proxyRequest_);
      setConditionalGetHeader(httpRequest.get(), getRequest(), getFileEntry(),
                              getOption());
      httpConnection_->sendRequest(std::move(httpRequest));
    }
    else {
      for (auto& segment : getSegments()) {
        if (!httpConnection_->isIssued(segment)) {
          httpConnection_->sendRequest(
              createHttpRequestForSegment(
                  getRequest(), getFileEntry(), getRequestGroup(),
                  getDownloadEngine(), getOption(), proxyRequest_,
                  getPieceStorage(), segment));
        }
      }
    }
  }
  else {
    httpConnection_->sendPendingData();
  }
  if (httpConnection_->sendBufferIsEmpty()) {
    getDownloadEngine()->addCommand(make_unique<HttpResponseCommand>(
        getCuid(), getRequest(), getFileEntry(), getRequestGroup(),
        httpConnection_, getDownloadEngine(), getSocket()));
    return true;
  }
  else {
    setReadCheckSocketIf(getSocket(), getSocket()->wantRead());
    setWriteCheckSocketIf(getSocket(), getSocket()->wantWrite());
    addCommandSelf();
    return false;
  }
}

void HttpRequestCommand::setProxyRequest(
    const std::shared_ptr<Request>& proxyRequest)
{
  proxyRequest_ = proxyRequest;
}

} // namespace aria2
