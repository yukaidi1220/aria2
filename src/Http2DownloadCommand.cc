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

#  include <utility>

#  include "DlAbortEx.h"
#  include "DownloadEngine.h"
#  include "Http2SingleStreamExchange.h"
#  include "HttpHeader.h"
#  include "HttpResponse.h"
#  include "SocketCore.h"
#  include "StreamFilter.h"
#  include "a2functional.h"
#  include "fmt.h"

namespace aria2 {

namespace {
const size_t BODY_CHUNK_SIZE = 16_k;
} // namespace

Http2DownloadCommand::Http2DownloadCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    std::shared_ptr<Http2SingleStreamExchange> exchange,
    std::unique_ptr<HttpResponse> httpResponse,
    std::unique_ptr<StreamFilter> streamFilter, DownloadEngine* e,
    const std::shared_ptr<SocketCore>& s)
    : AbstractCommand(cuid, req, fileEntry, requestGroup, e, s),
      exchange_(std::move(exchange)),
      httpResponse_(std::move(httpResponse)),
      streamFilter_(std::move(streamFilter)),
      expectedBodyLength_(0),
      bodyLength_(0),
      expectedBodyLengthKnown_(false)
{
  if (httpResponse_ && httpResponse_->getHttpHeader() &&
      httpResponse_->getHttpHeader()->defined(HttpHeader::CONTENT_LENGTH)) {
    expectedBodyLength_ = httpResponse_->getContentLength();
    expectedBodyLengthKnown_ = true;
  }
}

Http2DownloadCommand::~Http2DownloadCommand() = default;

bool Http2DownloadCommand::executeInternal()
{
  exchange_->pump();

  auto state = exchange_->getState();
  if (state.errorCode != 0) {
    throw DL_ABORT_EX(
        fmt("HTTP/2 stream failed while downloading body: errorCode=%u",
            static_cast<unsigned int>(state.errorCode)));
  }

  for (;;) {
    auto body = exchange_->popResponseBody(BODY_CHUNK_SIZE);
    if (body.empty()) {
      break;
    }
    bodyLength_ += static_cast<int64_t>(body.size());
    if (expectedBodyLengthKnown_ && bodyLength_ > expectedBodyLength_) {
      throw DL_ABORT_EX("HTTP/2 response body exceeds Content-Length");
    }
  }

  state = exchange_->getState();
  if (state.errorCode != 0) {
    throw DL_ABORT_EX(
        fmt("HTTP/2 stream failed while downloading body: errorCode=%u",
            static_cast<unsigned int>(state.errorCode)));
  }

  if (state.streamClosed) {
    if (expectedBodyLengthKnown_ && bodyLength_ != expectedBodyLength_) {
      throw DL_ABORT_EX("HTTP/2 stream closed before response body completed");
    }
    exchange_->popResponseEvent();
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
  requeueSelf();
  return false;
}

bool Http2DownloadCommand::noCheck() const
{
  auto state = exchange_->getState();
  return state.bodyLength > 0 || state.streamClosed || state.errorCode != 0;
}

void Http2DownloadCommand::requeueSelf() { addCommandSelf(); }

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
