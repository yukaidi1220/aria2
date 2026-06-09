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
#include "HttpInitiateConnectionCommand.h"

#include <functional>

#include "Request.h"
#include "RequestGroup.h"
#include "DownloadEngine.h"
#include "HttpConnection.h"
#include "HttpRequest.h"
#ifdef HAVE_LIBNGHTTP2
#  include "Http2MultiplexExchange.h"
#  include "Http2ResponseCommand.h"
#  include "HttpRequestFactory.h"
#  include "HttpProtocol.h"
#endif // HAVE_LIBNGHTTP2
#include "Segment.h"
#include "HttpRequestCommand.h"
#include "HttpProxyRequestCommand.h"
#include "DlAbortEx.h"
#include "Option.h"
#include "Logger.h"
#include "LogFactory.h"
#include "SocketCore.h"
#include "HttpTLSHandshakeParams.h"
#include "RecoverableException.h"
#include "message.h"
#include "prefs.h"
#include "A2STR.h"
#include "util.h"
#include "fmt.h"
#include "SocketRecvBuffer.h"
#include "BackupIPv4ConnectCommand.h"
#include "ConnectCommand.h"
#include "HttpRequestConnectChain.h"
#include "HttpProxyRequestConnectChain.h"

namespace aria2 {

namespace {

#ifdef ENABLE_SSL
std::function<bool(const std::shared_ptr<SocketCore>&)>
createTLSSocketReusePredicate(const Request* request, const Option* option)
{
  if (request->getProtocol() != "https") {
    return std::function<bool(const std::shared_ptr<SocketCore>&)>();
  }
  auto tlsParams = createHttpTLSHandshakeParams(request, option);
  return [tlsParams](const std::shared_ptr<SocketCore>& socket) {
    return socket->matchesTLSHandshakeParams(tlsParams);
  };
}
#endif // ENABLE_SSL

#if defined(ENABLE_SSL) && defined(HAVE_LIBNGHTTP2)
std::function<bool(const std::shared_ptr<SocketCore>&)>
createTLSHttp2OriginCoalescingPredicate(const Request* request,
                                        RequestGroup* requestGroup,
                                        const Option* option)
{
  if (request->getProtocol() != "https" ||
      request->http2OriginCoalescingBlocked()) {
    return std::function<bool(const std::shared_ptr<SocketCore>&)>();
  }

  auto tlsParams = createHttpTLSHandshakeParams(request, option);
  if (tlsParams.verifyHost.empty() || tlsParams.sniHostOverridden) {
    return std::function<bool(const std::shared_ptr<SocketCore>&)>();
  }
  auto protocol = request->getProtocol();
  auto host = request->getHost();
  auto port = request->getPort();

  return [requestGroup, protocol, host, port,
          tlsParams](const std::shared_ptr<SocketCore>& socket) {
    if (socket->getSelectedAlpnProtocol() != HTTP_ALPN_H2 ||
        !socket->matchesTLSHandshakeParamsForOriginCoalescing(tlsParams) ||
        !socket->peerCertificateMatchesHostname(tlsParams.verifyHost)) {
      return false;
    }
    try {
      auto peerInfo = socket->getPeerInfo();
      if (requestGroup &&
          requestGroup->http2OriginCoalescingPeerBlocked(
              protocol, host, port, tlsParams.verifyHost, peerInfo.addr,
              peerInfo.port)) {
        A2_LOG_NETWORK(
            fmt("Skipping blocked HTTP/2 origin coalescing for %s:%u via "
                "%s:%u",
                host.c_str(), port, peerInfo.addr.c_str(), peerInfo.port));
        return false;
      }
      return true;
    }
    catch (RecoverableException& e) {
      A2_LOG_INFO_EX(
          "Getting peer info failed. HTTP/2 coalescing canceled.", e);
      return false;
    }
  };
}
#endif // ENABLE_SSL && HAVE_LIBNGHTTP2

std::shared_ptr<SocketCore>
popReusablePooledSocket(DownloadEngine* e, const Request* request,
                        const Option* option,
                        const std::shared_ptr<Request>& proxyRequest)
{
#ifdef ENABLE_SSL
  auto predicate = createTLSSocketReusePredicate(request, option);
  if (predicate) {
    return e->popPooledSocket(request->getHost(), request->getPort(),
                              proxyRequest->getHost(), proxyRequest->getPort(),
                              predicate);
  }
#else  // !ENABLE_SSL
  (void)option;
#endif // ENABLE_SSL
  return e->popPooledSocket(request->getHost(), request->getPort(),
                            proxyRequest->getHost(),
                            proxyRequest->getPort());
}

std::shared_ptr<SocketCore>
popReusablePooledSocket(DownloadEngine* e, const Request* request,
                        const Option* option,
                        const std::vector<std::string>& resolvedAddresses)
{
#ifdef ENABLE_SSL
  auto predicate = createTLSSocketReusePredicate(request, option);
  if (predicate) {
    return e->popPooledSocket(resolvedAddresses, request->getPort(),
                              predicate);
  }
#else  // !ENABLE_SSL
  (void)option;
#endif // ENABLE_SSL
  return e->popPooledSocket(resolvedAddresses, request->getPort());
}

#ifdef HAVE_LIBNGHTTP2
bool canReuseActiveHttp2Connection(
    const Request* request, const Option* option, bool proxyReusable)
{
  if (request->getProtocol() != "https" ||
      !option->getAsBool(PREF_ENABLE_HTTP2)) {
    return false;
  }
  return proxyReusable;
}
#endif // HAVE_LIBNGHTTP2

} // namespace

HttpInitiateConnectionCommand::HttpInitiateConnectionCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    DownloadEngine* e)
    : InitiateConnectionCommand(cuid, req, fileEntry, requestGroup, e)
{
}

HttpInitiateConnectionCommand::~HttpInitiateConnectionCommand() = default;

std::unique_ptr<Command> HttpInitiateConnectionCommand::createNextCommand(
    const std::string& hostname, const std::string& addr, uint16_t port,
    const std::vector<std::string>& resolvedAddresses,
    const std::shared_ptr<Request>& proxyRequest)
{
#ifdef HAVE_LIBNGHTTP2
  bool proxyReusable =
      !proxyRequest ||
      resolveProxyMethod(getRequest()->getProtocol()) == V_TUNNEL;
  if (canReuseActiveHttp2Connection(getRequest().get(), getOption().get(),
                                    proxyReusable) &&
      getSegments().size() <= 1) {
#  ifdef ENABLE_SSL
    auto predicate =
        createTLSSocketReusePredicate(getRequest().get(), getOption().get());
    auto coalescingPredicate = createTLSHttp2OriginCoalescingPredicate(
        getRequest().get(), getRequestGroup(), getOption().get());
#  else  // !ENABLE_SSL
    std::function<bool(const std::shared_ptr<SocketCore>&)> predicate;
    std::function<bool(const std::shared_ptr<SocketCore>&)>
        coalescingPredicate;
#  endif // ENABLE_SSL
    if (proxyRequest) {
      coalescingPredicate =
          std::function<bool(const std::shared_ptr<SocketCore>&)>();
    }
    auto activeHttp2 = getDownloadEngine()->findActiveHttp2Connection(
        getRequestGroup(), getRequest().get(), hostname, addr, port,
        predicate, coalescingPredicate);
    if (activeHttp2.isActive()) {
      getRequest()->setConnectedAddrInfo(hostname, addr, port);
      getRequest()->confirmConnectedAddrInfo();
      getRequest()->setHttp2OriginCoalesced(activeHttp2.originCoalesced);

      std::unique_ptr<HttpRequest> httpRequest;
      if (getSegments().empty()) {
        httpRequest = createHttpRequest(
            getRequest(), getFileEntry(), std::shared_ptr<Segment>(),
            getOption(), getRequestGroup(), getDownloadEngine(),
            std::shared_ptr<Request>());
        setConditionalGetHeader(httpRequest.get(), getRequest(), getFileEntry(),
                                getOption());
      }
      else {
        httpRequest = createHttpRequestForSegment(
            getRequest(), getFileEntry(), getRequestGroup(),
            getDownloadEngine(), getOption(), std::shared_ptr<Request>(),
            getPieceStorage(), getSegments().front());
      }

      auto streamId = activeHttp2.exchange->submitRequest(*httpRequest);
      return make_unique<Http2ResponseCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(),
          activeHttp2.exchange, streamId, std::move(httpRequest),
          getDownloadEngine(), activeHttp2.socket, false,
          activeHttp2.context);
    }

    auto idleHttp2 = getDownloadEngine()->popIdleHttp2Connection(
        getRequestGroup(), getRequest().get(), hostname, addr, port,
        predicate, coalescingPredicate);
    if (idleHttp2.isActive()) {
      getRequest()->setConnectedAddrInfo(hostname, addr, port);
      getRequest()->confirmConnectedAddrInfo();
      getRequest()->setHttp2OriginCoalesced(idleHttp2.originCoalesced);

      std::unique_ptr<HttpRequest> httpRequest;
      if (getSegments().empty()) {
        httpRequest = createHttpRequest(
            getRequest(), getFileEntry(), std::shared_ptr<Segment>(),
            getOption(), getRequestGroup(), getDownloadEngine(),
            std::shared_ptr<Request>());
        setConditionalGetHeader(httpRequest.get(), getRequest(), getFileEntry(),
                                getOption());
      }
      else {
        httpRequest = createHttpRequestForSegment(
            getRequest(), getFileEntry(), getRequestGroup(),
            getDownloadEngine(), getOption(), std::shared_ptr<Request>(),
            getPieceStorage(), getSegments().front());
      }

      auto streamId = idleHttp2.exchange->submitRequest(*httpRequest);
      getDownloadEngine()->registerActiveHttp2Connection(getRequest().get(),
                                                         idleHttp2.context);
      return make_unique<Http2ResponseCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(),
          idleHttp2.exchange, streamId, std::move(httpRequest),
          getDownloadEngine(), idleHttp2.socket, false, idleHttp2.context);
    }
  }
#endif // HAVE_LIBNGHTTP2

  if (proxyRequest) {
    std::shared_ptr<SocketCore> pooledSocket =
        popReusablePooledSocket(getDownloadEngine(), getRequest().get(),
                                getOption().get(), proxyRequest);
    std::string proxyMethod = resolveProxyMethod(getRequest()->getProtocol());
    if (!pooledSocket) {
      A2_LOG_INFO(fmt(MSG_CONNECTING_TO_SERVER, getCuid(), addr.c_str(), port));
      A2_LOG_NETWORK(
          fmt("CUID#%" PRId64 " - Connecting to %s:%u (via proxy)",
              getCuid(), addr.c_str(), port));
      createSocket();
      getSocket()->establishConnection(addr, port);

      getRequest()->setConnectedAddrInfo(hostname, addr, port);
      auto c = make_unique<ConnectCommand>(
          getCuid(), getRequest(), proxyRequest, getFileEntry(),
          getRequestGroup(), getDownloadEngine(), getSocket());
      if (proxyMethod == V_TUNNEL) {
        c->setControlChain(std::make_shared<HttpProxyRequestConnectChain>());
      }
      else if (proxyMethod == V_GET) {
        c->setControlChain(std::make_shared<HttpRequestConnectChain>());
      }
      else {
        // Unreachable
        assert(0);
      }
      setupBackupConnection(hostname, addr, port, c.get());
      return std::move(c);
    }
    else {
      setConnectedAddrInfo(getRequest(), hostname, pooledSocket);
      auto c = make_unique<HttpRequestCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(),
          std::make_shared<HttpConnection>(
              getCuid(), pooledSocket,
              std::make_shared<SocketRecvBuffer>(pooledSocket)),
          getDownloadEngine(), pooledSocket);
      if (proxyMethod == V_GET) {
        c->setProxyRequest(proxyRequest);
      }
      return std::move(c);
    }
  }
  else {
    std::shared_ptr<SocketCore> pooledSocket =
        popReusablePooledSocket(getDownloadEngine(), getRequest().get(),
                                getOption().get(), resolvedAddresses);
    if (!pooledSocket) {
      A2_LOG_INFO(fmt(MSG_CONNECTING_TO_SERVER, getCuid(), addr.c_str(), port));
      A2_LOG_NETWORK(
          fmt("CUID#%" PRId64 " - Connecting to %s:%u",
              getCuid(), addr.c_str(), port));
      createSocket();
      getSocket()->establishConnection(addr, port);

      getRequest()->setConnectedAddrInfo(hostname, addr, port);
      auto c = make_unique<ConnectCommand>(getCuid(), getRequest(),
                                           proxyRequest, // must be null
                                           getFileEntry(), getRequestGroup(),
                                           getDownloadEngine(), getSocket());
      c->setControlChain(std::make_shared<HttpRequestConnectChain>());
      setupBackupConnection(hostname, addr, port, c.get());
      return std::move(c);
    }
    else {
      setSocket(pooledSocket);
      setConnectedAddrInfo(getRequest(), hostname, pooledSocket);

      return make_unique<HttpRequestCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(),
          std::make_shared<HttpConnection>(
              getCuid(), getSocket(),
              std::make_shared<SocketRecvBuffer>(getSocket())),
          getDownloadEngine(), getSocket());
    }
  }
}

} // namespace aria2
