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
#  include "Http2SingleStreamExchange.h"
#  include "HttpRequest.h"
#  include "HttpResponse.h"
#  include "SocketCore.h"
#  include "StreamFilter.h"

#  include <utility>

namespace aria2 {

Http2ResponseCommand::Http2ResponseCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    std::shared_ptr<Http2SingleStreamExchange> exchange,
    std::unique_ptr<HttpRequest> httpRequest, DownloadEngine* e,
    const std::shared_ptr<SocketCore>& s)
    : HttpResponseCommand(cuid, req, fileEntry, requestGroup, e, s, nullptr),
      exchange_(std::move(exchange)),
      httpRequest_(std::move(httpRequest))
{
}

Http2ResponseCommand::~Http2ResponseCommand() = default;

bool Http2ResponseCommand::executeInternal()
{
  exchange_->pump();

  auto state = exchange_->getState();
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

  auto httpResponse = exchange_->createHttpResponse();
  httpResponse->setCuid(getCuid());
  httpResponse->setHttpRequest(std::move(httpRequest_));
  return processHttpResponse(std::move(httpResponse));
}

std::unique_ptr<Command> Http2ResponseCommand::createHttpDownloadCommand(
    std::unique_ptr<HttpResponse> httpResponse,
    std::unique_ptr<StreamFilter> streamFilter)
{
  (void)httpResponse;
  (void)streamFilter;
  throw DL_ABORT_EX("HTTP/2 response body download is not implemented");
}

bool Http2ResponseCommand::skipResponseBody(
    std::unique_ptr<HttpResponse> httpResponse)
{
  (void)httpResponse;
  throw DL_ABORT_EX("HTTP/2 response body skip is not implemented");
}

void Http2ResponseCommand::poolConnection() {}

void Http2ResponseCommand::requeueSelf() { addCommandSelf(); }

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
