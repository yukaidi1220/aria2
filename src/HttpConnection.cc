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
#include "HttpConnection.h"

#include "message.h"
#include "prefs.h"
#include "LogFactory.h"
#include "DlRetryEx.h"
#include "DlAbortEx.h"
#include "Request.h"
#include "Segment.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpHeaderProcessor.h"
#include "HttpHeader.h"
#include "Logger.h"
#include "SocketCore.h"
#include "HttpLog.h"
#include "Option.h"
#include "CookieStorage.h"
#include "AuthConfigFactory.h"
#include "AuthConfig.h"
#include "a2functional.h"
#include "fmt.h"
#include "SocketRecvBuffer.h"
#include "array_fun.h"

namespace aria2 {

namespace {

std::string getRemoteEndpointForLog(const std::shared_ptr<SocketCore>& socket)
{
  if (!socket) {
    return "unavailable";
  }

  try {
    return formatEndpointForLog(socket->getPeerInfo());
  }
  catch (DlAbortEx& ex) {
    return fmt("unavailable (%s)", ex.what());
  }
}

} // namespace

HttpRequestEntry::HttpRequestEntry(std::unique_ptr<HttpRequest> httpRequest)
    : httpRequest_{std::move(httpRequest)},
      proc_{
          make_unique<HttpHeaderProcessor>(HttpHeaderProcessor::CLIENT_PARSER)}
{
}

void HttpRequestEntry::resetHttpHeaderProcessor()
{
  proc_ = make_unique<HttpHeaderProcessor>(HttpHeaderProcessor::CLIENT_PARSER);
}

std::unique_ptr<HttpRequest> HttpRequestEntry::popHttpRequest()
{
  return std::move(httpRequest_);
}

const std::unique_ptr<HttpHeaderProcessor>&
HttpRequestEntry::getHttpHeaderProcessor() const
{
  return proc_;
}

HttpConnection::HttpConnection(
    cuid_t cuid, const std::shared_ptr<SocketCore>& socket,
    const std::shared_ptr<SocketRecvBuffer>& socketRecvBuffer)
    : cuid_(cuid),
      socket_(socket),
      socketRecvBuffer_(socketRecvBuffer),
      socketBuffer_(socket)
{
}

HttpConnection::~HttpConnection() = default;

void HttpConnection::sendRequest(std::unique_ptr<HttpRequest> httpRequest,
                                 std::string request)
{
  A2_LOG_INFO(
      fmt(MSG_SENDING_REQUEST, cuid_,
          eraseHttpHeaderConfidentialInfo(request).c_str()));
  A2_LOG_NETWORK(formatHttpRequestHeaderLog(cuid_, "HTTP/1.1", request));
  socketBuffer_.pushStr(std::move(request));
  socketBuffer_.send();
  outstandingHttpRequests_.push_back(
      make_unique<HttpRequestEntry>(std::move(httpRequest)));
}

void HttpConnection::sendRequest(std::unique_ptr<HttpRequest> httpRequest)
{
  auto req = httpRequest->createRequest();
  sendRequest(std::move(httpRequest), std::move(req));
}

void HttpConnection::sendProxyRequest(std::unique_ptr<HttpRequest> httpRequest)
{
  auto req = httpRequest->createProxyRequest();
  sendRequest(std::move(httpRequest), std::move(req));
}

std::unique_ptr<HttpResponse> HttpConnection::receiveResponse()
{
  if (outstandingHttpRequests_.empty()) {
    throw DL_ABORT_EX(EX_NO_HTTP_REQUEST_ENTRY_FOUND);
  }
  if (socketRecvBuffer_->bufferEmpty()) {
    if (socketRecvBuffer_->recv() == 0 && !socket_->wantRead() &&
        !socket_->wantWrite()) {
      A2_LOG_NETWORK(
          fmt("HTTP: CUID#%" PRId64 " - Server closed connection", cuid_));
      throw DL_RETRY_EX(EX_GOT_EOF);
    }
  }

  const auto& proc = outstandingHttpRequests_.front()->getHttpHeaderProcessor();
  if (proc->parse(socketRecvBuffer_->getBuffer(),
                  socketRecvBuffer_->getBufferLength())) {
    auto remoteEndpoint = getRemoteEndpointForLog(socket_);
    A2_LOG_INFO(formatHttpResponseReceivedLog(
        cuid_, remoteEndpoint,
        eraseHttpHeaderConfidentialInfo(proc->getHeaderString())));
    auto result = proc->getResult();
    A2_LOG_NETWORK(formatHttpResponseStatusLog(cuid_, result->getStatusCode(),
                                               remoteEndpoint));
    if (result->getStatusCode() / 100 == 1) {
      socketRecvBuffer_->drain(proc->getLastBytesProcessed());
      outstandingHttpRequests_.front()->resetHttpHeaderProcessor();
      return nullptr;
    }

    auto httpResponse = make_unique<HttpResponse>();
    httpResponse->setCuid(cuid_);
    httpResponse->setHttpHeader(std::move(result));
    httpResponse->setHttpRequest(
        outstandingHttpRequests_.front()->popHttpRequest());
    socketRecvBuffer_->drain(proc->getLastBytesProcessed());
    outstandingHttpRequests_.pop_front();
    return httpResponse;
  }

  socketRecvBuffer_->drain(proc->getLastBytesProcessed());
  return nullptr;
}

bool HttpConnection::isIssued(const std::shared_ptr<Segment>& segment) const
{
  for (const auto& entry : outstandingHttpRequests_) {
    if (*entry->getHttpRequest()->getSegment() == *segment) {
      return true;
    }
  }
  return false;
}

bool HttpConnection::sendBufferIsEmpty() const
{
  return socketBuffer_.sendBufferIsEmpty();
}

void HttpConnection::sendPendingData() { socketBuffer_.send(); }

} // namespace aria2
