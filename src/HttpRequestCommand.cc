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

#include "Request.h"
#include "DownloadEngine.h"
#include "RequestGroup.h"
#include "HttpResponseCommand.h"
#ifdef HAVE_LIBNGHTTP2
#  include "Http2ResponseCommand.h"
#  include "Http2SingleStreamExchange.h"
#  include "Http2SocketCoreTransport.h"
#endif // HAVE_LIBNGHTTP2
#include "HttpConnection.h"
#include "HttpRequest.h"
#include "SegmentMan.h"
#include "Segment.h"
#include "Option.h"
#include "SocketCore.h"
#include "HostMapping.h"
#include "HttpProtocol.h"
#include "HttpTLSHandshakeParams.h"
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

namespace {
std::unique_ptr<HttpRequest>
createHttpRequest(const std::shared_ptr<Request>& req,
                  const std::shared_ptr<FileEntry>& fileEntry,
                  const std::shared_ptr<Segment>& segment,
                  const std::shared_ptr<Option>& option, const RequestGroup* rg,
                  const DownloadEngine* e,
                  const std::shared_ptr<Request>& proxyRequest,
                  int64_t endOffset = 0)
{
  auto httpRequest = make_unique<HttpRequest>();
  httpRequest->setUserAgent(option->get(PREF_USER_AGENT));
  httpRequest->setRequest(req);
  httpRequest->setFileEntry(fileEntry);
  httpRequest->setSegment(segment);
  httpRequest->addHeader(option->get(PREF_HEADER));
  httpRequest->setCookieStorage(e->getCookieStorage().get());
  httpRequest->setAuthConfigFactory(e->getAuthConfigFactory().get());
  httpRequest->setOption(option.get());
  httpRequest->setProxyRequest(proxyRequest);
  httpRequest->setAcceptMetalink(rg->getDownloadContext()->getAcceptMetalink());
  httpRequest->setNoWantDigest(option->getAsBool(PREF_NO_WANT_DIGEST_HEADER));

  if (option->getAsBool(PREF_HTTP_ACCEPT_GZIP)) {
    httpRequest->enableAcceptGZip();
  }
  else {
    httpRequest->disableAcceptGZip();
  }
  if (option->getAsBool(PREF_HTTP_NO_CACHE)) {
    httpRequest->enableNoCache();
  }
  else {
    httpRequest->disableNoCache();
  }
  if (endOffset > 0) {
    httpRequest->setEndOffsetOverride(endOffset);
  }
  return httpRequest;
}

void setConditionalGetHeader(HttpRequest* httpRequest,
                             const std::shared_ptr<Request>& request,
                             const std::shared_ptr<FileEntry>& fileEntry,
                             const std::shared_ptr<Option>& option)
{
  if (!option->getAsBool(PREF_CONDITIONAL_GET) ||
      (request->getProtocol() != "http" && request->getProtocol() != "https")) {
    return;
  }

  std::string path;
  if (fileEntry->getPath().empty()) {
    auto& file = request->getFile();
    path = util::createSafePath(
        option->get(PREF_DIR),
        (request->getFile().empty()
             ? Request::DEFAULT_FILE
             : util::percentDecode(std::begin(file), std::end(file))));
  }
  else {
    path = fileEntry->getPath();
  }

  File ctrlfile(path + DefaultBtProgressInfoFile::getSuffix());
  File file(path);
  if (!ctrlfile.exists() && file.exists()) {
    httpRequest->setIfModifiedSinceHeader(file.getModifiedTime().toHTTPDate());
  }
}

int64_t getSegmentEndOffset(const std::shared_ptr<Request>& request,
                            const std::shared_ptr<FileEntry>& fileEntry,
                            RequestGroup* requestGroup,
                            const std::shared_ptr<PieceStorage>& pieceStorage,
                            const std::shared_ptr<Segment>& segment)
{
  if (request->getProtocol() != "ftp" && requestGroup->getTotalLength() > 0 &&
      pieceStorage) {
    size_t nextIndex =
        pieceStorage->getNextUsedIndex(segment->getIndex());
    return std::min(
        fileEntry->getLength(),
        fileEntry->gtoloff(static_cast<int64_t>(segment->getSegmentLength()) *
                           nextIndex));
  }
  return 0;
}

std::unique_ptr<HttpRequest>
createHttpRequestForSegment(const std::shared_ptr<Request>& request,
                            const std::shared_ptr<FileEntry>& fileEntry,
                            RequestGroup* requestGroup, DownloadEngine* e,
                            const std::shared_ptr<Option>& option,
                            const std::shared_ptr<Request>& proxyRequest,
                            const std::shared_ptr<PieceStorage>& pieceStorage,
                            const std::shared_ptr<Segment>& segment)
{
  auto endOffset = getSegmentEndOffset(request, fileEntry, requestGroup,
                                       pieceStorage, segment);
  return createHttpRequest(request, fileEntry, segment, option, requestGroup, e,
                           proxyRequest, endOffset);
}
} // namespace

#ifdef ENABLE_SSL
namespace {
bool isTLSHandshakeFailure(const RecoverableException& e)
{
  return util::startsWith(std::string(e.what()), "SSL/TLS handshake failure:");
}

bool tryRetryTLSHandshakeWithNextAddress(
    const std::shared_ptr<Request>& req,
    const std::shared_ptr<Option>& option, DownloadEngine* e, cuid_t cuid,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup)
{
  if (req->getProtocol() != "https" ||
      option->getAsBool(PREF_CHECK_CERTIFICATE) ||
      req->getConnectedAddr().empty()) {
    return false;
  }
  auto tlsParams = createHttpTLSHandshakeParams(req.get(), option.get());
  if (tlsParams.sniHost != tlsParams.verifyHost) {
    return false;
  }

  const auto& connectedAddr = req->getConnectedAddr();
  const auto connectedPort = req->getConnectedPort();
  e->markBadIPAddress(req->getConnectedHostname(), connectedAddr,
                      connectedPort);
  if (e->findCachedIPAddress(req->getConnectedHostname(),
                             connectedPort)
          .empty()) {
    if (getMappedAddresses(req->getConnectedHostname(), option.get())
        .empty()) {
      e->removeCachedIPAddress(req->getConnectedHostname(), connectedPort);
    }
    return false;
  }

  A2_LOG_NETWORK(fmt("CUID#%" PRId64
                     " - TLS handshake failed for %s:%u, retrying another "
                     "resolved address",
                     cuid, connectedAddr.c_str(), connectedPort));
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
        if (retryTLSHandshakeWithNextAddress_ && !proxyRequest_ &&
            isTLSHandshakeFailure(e) &&
            tryRetryTLSHandshakeWithNextAddress(
                getRequest(), getOption(), getDownloadEngine(), getCuid(),
                getFileEntry(), getRequestGroup())) {
          return true;
        }
        throw;
      }
      httpProtocol = decideHttpProtocolFromSelectedAlpn(
          getSocket()->getSelectedAlpnProtocol(),
          getOption()->getAsBool(PREF_ENABLE_HTTP2));
      A2_LOG_NETWORK(
          fmt("CUID#%" PRId64 " - HTTPS connection to %s established",
              getCuid(), getRequest()->getHost().c_str()));
    }
#endif // ENABLE_SSL
#ifdef HAVE_LIBNGHTTP2
    if (httpProtocol == HTTP_PROTOCOL_H2) {
      if (getSegments().size() > 1) {
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

      auto exchange = std::make_shared<Http2SingleStreamExchange>(
          make_unique<Http2SocketCoreTransport>(getSocket()));
      exchange->submitRequest(*httpRequest);
      exchange->flushOutboundData();
      getDownloadEngine()->addCommand(make_unique<Http2ResponseCommand>(
          getCuid(), getRequest(), getFileEntry(), getRequestGroup(), exchange,
          std::move(httpRequest), getDownloadEngine(), getSocket()));
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
