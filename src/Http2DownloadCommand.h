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
#ifndef D_HTTP2_DOWNLOAD_COMMAND_H
#define D_HTTP2_DOWNLOAD_COMMAND_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include <memory>
#  include <string>

#  include "DownloadCommand.h"

namespace aria2 {

class Http2MultiplexExchange;
class Http2ConnectionContext;
class HttpResponse;
class StreamFilter;

class Http2DownloadCommand : public DownloadCommand {
private:
  std::shared_ptr<Http2MultiplexExchange> exchange_;
  int32_t streamId_;
  std::unique_ptr<HttpResponse> httpResponse_;
  std::shared_ptr<Http2ConnectionContext> connectionContext_;
  int64_t expectedBodyLength_;
  int64_t bodyLength_;
  bool expectedBodyLengthKnown_;
  std::string pendingBody_;

  void poolIdleConnection();

protected:
  bool executeInternal() CXX11_OVERRIDE;
  bool noCheck() const CXX11_OVERRIDE;
  int64_t getRequestEndOffset() const CXX11_OVERRIDE;
  bool prepareForNextSegment() CXX11_OVERRIDE;
  void requeueSelf() CXX11_OVERRIDE;

public:
  Http2DownloadCommand(
      cuid_t cuid, const std::shared_ptr<Request>& req,
      const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
      std::shared_ptr<Http2MultiplexExchange> exchange, int32_t streamId,
      std::unique_ptr<HttpResponse> httpResponse,
      std::unique_ptr<StreamFilter> streamFilter, DownloadEngine* e,
      const std::shared_ptr<SocketCore>& s, bool incNumConnection = true,
      std::shared_ptr<Http2ConnectionContext> connectionContext = nullptr);
  ~Http2DownloadCommand();
};

} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_DOWNLOAD_COMMAND_H
