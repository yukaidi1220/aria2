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
 * file(s) with this exception, you may extend this exception statement from
 * your version, but you are not obligated to do so.  If you do not wish to do
 * so, delete this exception statement from your version.  If you delete this
 * exception statement from all source files in the program, then also delete
 * it here.
 */
/* copyright --> */
#include "Http2ResponseCommand.h"

#ifdef HAVE_LIBNGHTTP2

#  include "DlAbortEx.h"
#  include "DownloadEngine.h"
#  include "Http2ConnectionContext.h"
#  include "Http2DownloadCommand.h"
#  include "Http2MultiplexExchange.h"
#  include "HttpHeader.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "HttpSkipResponseCommand.h"
#  include "Request.h"
#  include "SocketCore.h"
#  include "StreamFilter.h"
#  include "a2functional.h"

#  include <utility>

namespace aria2 {

namespace {
const size_t SKIP_BODY_CHUNK_SIZE = 1_m;

void scheduleHttp2Now(Http2MultiplexExchange* exchange, DownloadEngine* e)
{
  exchange->activateCommands(e);
}
} // namespace

Http2ResponseCommand::Http2ResponseCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    std::shared_ptr<Http2MultiplexExchange> exchange, int32_t streamId,
    std::unique_ptr<HttpRequest> httpRequest, DownloadEngine* e,
    const std::shared_ptr<SocketCore>& s, bool incNumConnection,
    std::shared_ptr<Http2ConnectionContext> connectionContext)
    : HttpResponseCommand(cuid, req, fileEntry, requestGroup, e, s, nullptr,
                          incNumConnection),
      exchange_(std::move(exchange)),
      streamId_(streamId),
      httpRequest_(std::move(httpRequest)),
      connectionContext_(std::move(connectionContext)),
      expectedSkipBodyLength_(0),
      skippedBodyLength_(0),
      expectedSkipBodyLengthKnown_(false),
      incNumConnection_(incNumConnection)
{
  exchange_->registerCommand(this);
  setStatusActive();
  e->setNoWait(true);
}

Http2ResponseCommand::~Http2ResponseCommand()
{
  exchange_->unregisterCommand(this);
}

bool Http2ResponseCommand::executeInternal()
{
  if (skipHttpResponse_) {
    return drainSkippedResponseBody();
  }

  const bool progressed = exchange_->pump();
  if (progressed) {
    scheduleHttp2Now(exchange_.get(), getDownloadEngine());
  }

  auto state = exchange_->getState(streamId_);
  if (!state.headersComplete) {
    if (state.streamClosed) {
      throw DL_ABORT_EX("HTTP/2 stream closed before response headers");
    }
    if (state.errorCode != 0) {
      throw DL_ABORT_EX("HTTP/2 stream failed before response headers");
    }
    auto& socket = getSocket();
    if (socket) {
      setReadCheckSocketIf(socket, exchange_->wantRead());
      setWriteCheckSocketIf(socket, exchange_->wantWrite());
    }
    requeueSelf();
    return false;
  }

  if (!httpRequest_) {
    throw DL_ABORT_EX("HTTP/2 response was already processed");
  }

  auto httpResponse = exchange_->createHttpResponse(streamId_);
  if (!httpResponse) {
    throw DL_ABORT_EX("HTTP/2 response headers were not available");
  }
  httpResponse->setCuid(getCuid());
  httpResponse->setHttpRequest(std::move(httpRequest_));
  getRequest()->setPipeliningHint(false);
  auto processed = processHttpResponse(std::move(httpResponse));
  if (processed) {
    getDownloadEngine()->setNoWait(true);
  }
  return processed;
}

std::unique_ptr<Command> Http2ResponseCommand::createHttpDownloadCommand(
    std::unique_ptr<HttpResponse> httpResponse,
    std::unique_ptr<StreamFilter> streamFilter)
{
  auto command = make_unique<Http2DownloadCommand>(
      getCuid(), getRequest(), getFileEntry(), getRequestGroup(), exchange_,
      streamId_, std::move(httpResponse), std::move(streamFilter),
      getDownloadEngine(), getSocket(), incNumConnection_,
      connectionContext_);
  command->setStatusActive();
  return std::move(command);
}

bool Http2ResponseCommand::skipResponseBody(
    std::unique_ptr<HttpResponse> httpResponse)
{
  skipHttpResponse_ = std::move(httpResponse);
  skippedBodyLength_ = 0;
  expectedSkipBodyLength_ = 0;
  expectedSkipBodyLengthKnown_ = false;
  if (getRequest()->getMethod() != Request::METHOD_HEAD &&
      skipHttpResponse_->getHttpHeader()->defined(HttpHeader::CONTENT_LENGTH)) {
    expectedSkipBodyLength_ = skipHttpResponse_->getContentLength();
    expectedSkipBodyLengthKnown_ = true;
  }
  return drainSkippedResponseBody();
}

void Http2ResponseCommand::poolIdleConnection()
{
  if (getRequest()->supportsPersistentConnection() && connectionContext_ &&
      !exchange_->hasActiveStreams()) {
    getDownloadEngine()->poolIdleHttp2Connection(getRequest().get(),
                                                 connectionContext_);
  }
}

void Http2ResponseCommand::poolConnection()
{
  auto state = exchange_->getState(streamId_);
  if (state.streamClosed) {
    exchange_->popResponseEvent(streamId_);
  }
  poolIdleConnection();
}

void Http2ResponseCommand::requeueSelf()
{
  addCommandSelf();
}

bool Http2ResponseCommand::drainSkippedResponseBody()
{
  const bool progressed = exchange_->pump();
  if (progressed) {
    scheduleHttp2Now(exchange_.get(), getDownloadEngine());
  }

  auto state = exchange_->getState(streamId_);
  // When skipping a response body (e.g., for redirects or error pages),
  // the server may send fewer bytes than the declared Content-Length.
  // nghttp2 detects this as a content-length mismatch and sends RST_STREAM
  // with NGHTTP2_PROTOCOL_ERROR. This is acceptable when skipping.
  if (state.errorCode != 0 &&
      state.errorCode != NGHTTP2_PROTOCOL_ERROR) {
    throw DL_ABORT_EX("HTTP/2 stream failed while skipping response body");
  }

  for (;;) {
    auto body = exchange_->popResponseBody(streamId_, SKIP_BODY_CHUNK_SIZE);
    if (body.empty()) {
      break;
    }
    skippedBodyLength_ += static_cast<int64_t>(body.size());
    if (expectedSkipBodyLengthKnown_ &&
        skippedBodyLength_ > expectedSkipBodyLength_) {
      throw DL_ABORT_EX("HTTP/2 skipped response body exceeds Content-Length");
    }
  }

  state = exchange_->getState(streamId_);
  if (state.errorCode != 0 &&
      state.errorCode != NGHTTP2_PROTOCOL_ERROR) {
    throw DL_ABORT_EX("HTTP/2 stream failed while skipping response body");
  }
  if (state.streamClosed) {
    exchange_->popResponseEvent(streamId_);
    poolIdleConnection();
    return processSkippedHttpResponse(
        this, skipHttpResponse_, [this]() { return prepareForRetry(0); });
  }

  auto& socket = getSocket();
  if (socket) {
    setReadCheckSocketIf(socket, exchange_->wantRead());
    setWriteCheckSocketIf(socket, exchange_->wantWrite());
  }
  else {
    disableReadCheckSocket();
    disableWriteCheckSocket();
    getDownloadEngine()->setNoWait(true);
  }
  auto stateAfterPump = exchange_->getState(streamId_);
  if (stateAfterPump.bodyLength > 0 || stateAfterPump.streamClosed ||
      stateAfterPump.errorCode != 0 || exchange_->hasBufferedInboundData()) {
    scheduleHttp2Now(exchange_.get(), getDownloadEngine());
  }
  requeueSelf();
  return false;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
