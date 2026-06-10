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
#include "Http2DownloadCommand.h"

#ifdef HAVE_LIBNGHTTP2

#  include <chrono>
#  include <utility>

#  include "DlAbortEx.h"
#  include "DownloadEngine.h"
#  include "FileEntry.h"
#  include "Http2ConnectionContext.h"
#  include "Http2MultiplexExchange.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"
#  include "Option.h"
#  include "Range.h"
#  include "RequestGroup.h"
#  include "Segment.h"
#  include "SegmentMan.h"
#  include "SocketCore.h"
#  include "StreamFilter.h"
#  include "URISelector.h"
#  include "a2functional.h"
#  include "fmt.h"
#  include "prefs.h"

namespace aria2 {

namespace {
const size_t BODY_CHUNK_SIZE = 16_k;

void scheduleHttp2Now(Command* command, DownloadEngine* e)
{
  command->setStatus(Command::STATUS_ONESHOT_REALTIME);
  e->setNoWait(true);
  e->setRefreshInterval(std::chrono::milliseconds(0));
}
} // namespace

Http2DownloadCommand::Http2DownloadCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    std::shared_ptr<Http2MultiplexExchange> exchange, int32_t streamId,
    std::unique_ptr<HttpResponse> httpResponse,
    std::unique_ptr<StreamFilter> streamFilter, DownloadEngine* e,
    const std::shared_ptr<SocketCore>& s, bool incNumConnection,
    std::shared_ptr<Http2ConnectionContext> connectionContext)
    : DownloadCommand(cuid, req, fileEntry, requestGroup, e, s, nullptr,
                      incNumConnection),
      exchange_(std::move(exchange)),
      streamId_(streamId),
      httpResponse_(std::move(httpResponse)),
      connectionContext_(std::move(connectionContext)),
      expectedBodyLength_(0),
      bodyLength_(0),
      expectedBodyLengthKnown_(false)
{
  if (httpResponse_ && httpResponse_->getHttpHeader() &&
      httpResponse_->getHttpHeader()->defined(HttpHeader::CONTENT_LENGTH)) {
    expectedBodyLength_ = httpResponse_->getContentLength();
    expectedBodyLengthKnown_ = true;
  }
  setStartupIdleTime(
      std::chrono::seconds(getOption()->getAsInt(PREF_STARTUP_IDLE_TIME)));
  setLowestDownloadSpeedLimit(getOption()->getAsInt(PREF_LOWEST_SPEED_LIMIT));
  installStreamFilter(std::move(streamFilter));
  getRequestGroup()->getURISelector()->tuneDownloadCommand(
      getFileEntry()->getRemainingUris(), this);
}

Http2DownloadCommand::~Http2DownloadCommand() = default;

void Http2DownloadCommand::poolIdleConnection()
{
  if (getRequest()->supportsPersistentConnection() && connectionContext_ &&
      !exchange_->hasActiveStreams()) {
    getDownloadEngine()->poolIdleHttp2Connection(getRequest().get(),
                                                 connectionContext_);
  }
}

bool Http2DownloadCommand::executeInternal()
{
  if (downloadSpeedLimitExceeded()) {
    requeueSelf();
    disableReadCheckSocket();
    disableWriteCheckSocket();
    return false;
  }

  const bool progressed = exchange_->pump();
  if (progressed) {
    scheduleHttp2Now(this, getDownloadEngine());
  }

  auto state = exchange_->getState(streamId_);
  if (state.errorCode != 0) {
    throw DL_ABORT_EX(
        fmt("HTTP/2 stream failed while downloading body: errorCode=%u",
            static_cast<unsigned int>(state.errorCode)));
  }

  for (;;) {
    state = exchange_->getState(streamId_);
    if (pendingBody_.empty()) {
      pendingBody_ = exchange_->popResponseBody(streamId_, BODY_CHUNK_SIZE);
      if (pendingBody_.empty()) {
        break;
      }
      bodyLength_ += static_cast<int64_t>(pendingBody_.size());
      if (expectedBodyLengthKnown_ && bodyLength_ > expectedBodyLength_) {
        throw DL_ABORT_EX("HTTP/2 response body exceeds Content-Length");
      }
      if (!expectedBodyLengthKnown_ &&
          httpResponse_->getHttpHeader()->getRange().endByte == 0 &&
          getFileEntry()->getLength() > 0 &&
          bodyLength_ > getFileEntry()->getLength()) {
        throw DL_ABORT_EX("HTTP/2 response body exceeds file length");
      }
    }

    size_t consumed = 0;
    auto segmentCompletionMode =
        state.streamClosed ? SegmentCompletionMode::ALLOW
                           : SegmentCompletionMode::DEFER_AT_REQUEST_END;
    auto result = processData(
        reinterpret_cast<const unsigned char*>(pendingBody_.data()),
        pendingBody_.size(), false, consumed, segmentCompletionMode);
    pendingBody_.erase(0, consumed);
    if (result == ProcessDataResult::DONE) {
      if (!pendingBody_.empty()) {
        throw DL_ABORT_EX("HTTP/2 response body was not fully consumed");
      }
      if (state.streamClosed) {
        exchange_->popResponseEvent(streamId_);
        poolIdleConnection();
      }
      return true;
    }
    if (result == ProcessDataResult::REQUEUED) {
      continue;
    }
    if (consumed == 0) {
      break;
    }
  }

  state = exchange_->getState(streamId_);
  if (state.errorCode != 0) {
    throw DL_ABORT_EX(
        fmt("HTTP/2 stream failed while downloading body: errorCode=%u",
            static_cast<unsigned int>(state.errorCode)));
  }

  if (state.streamClosed) {
    if (expectedBodyLengthKnown_ && bodyLength_ != expectedBodyLength_) {
      throw DL_ABORT_EX("HTTP/2 stream closed before response body completed");
    }
    if (!pendingBody_.empty()) {
      throw DL_ABORT_EX("HTTP/2 response body was not fully consumed");
    }
    size_t consumed = 0;
    auto result = processData(nullptr, 0, true, consumed);
    if (result != ProcessDataResult::DONE) {
      throw DL_ABORT_EX("HTTP/2 response body did not complete on stream close");
    }
    exchange_->popResponseEvent(streamId_);
    poolIdleConnection();
    return true;
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
  state = exchange_->getState(streamId_);
  if (!pendingBody_.empty() || state.bodyLength > 0 || state.streamClosed ||
      state.errorCode != 0) {
    scheduleHttp2Now(this, getDownloadEngine());
  }
  requeueSelf();
  return false;
}

bool Http2DownloadCommand::noCheck() const
{
  auto state = exchange_->getState(streamId_);
  return DownloadCommand::noCheck() || !pendingBody_.empty() ||
         state.bodyLength > 0 || state.streamClosed || state.errorCode != 0;
}

int64_t Http2DownloadCommand::getRequestEndOffset() const
{
  auto range = httpResponse_->getHttpHeader()->getRange();
  if (range.endByte > 0) {
    return range.endByte + 1;
  }
  if (expectedBodyLengthKnown_) {
    return expectedBodyLength_;
  }
  if (getFileEntry()->getLength() > 0) {
    return getFileEntry()->getLength();
  }
  return range.endByte;
}

bool Http2DownloadCommand::prepareForNextSegment()
{
  if (getRequestGroup()->downloadFinished()) {
    return DownloadCommand::prepareForNextSegment();
  }

  if (getSegments().size() != 1) {
    return prepareForRetry(0);
  }

  auto tempSegment = getSegments().front();
  if (!tempSegment->complete()) {
    return prepareForRetry(0);
  }
  if (getRequestEndOffset() ==
      getFileEntry()->gtoloff(tempSegment->getPosition() +
                              tempSegment->getLength())) {
    return prepareForRetry(0);
  }

  auto nextSegment = getSegmentMan()->getSegmentWithIndex(
      getCuid(), tempSegment->getIndex() + 1);
  if (!nextSegment) {
    nextSegment = getSegmentMan()->getCleanSegmentIfOwnerIsIdle(
        getCuid(), tempSegment->getIndex() + 1);
  }
  if (!nextSegment || nextSegment->getWrittenLength() > 0) {
    return prepareForRetry(0);
  }

  refreshSegments();
  return false;
}

void Http2DownloadCommand::requeueSelf()
{
  addCommandSelf();
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
