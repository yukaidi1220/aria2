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
#include "HttpRequestFactory.h"

#include <algorithm>
#include <iterator>

#include "a2functional.h"
#include "DefaultBtProgressInfoFile.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "File.h"
#include "FileEntry.h"
#include "HttpRequest.h"
#include "Option.h"
#include "PieceStorage.h"
#include "Request.h"
#include "RequestGroup.h"
#include "Segment.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

std::unique_ptr<HttpRequest>
createHttpRequest(const std::shared_ptr<Request>& req,
                  const std::shared_ptr<FileEntry>& fileEntry,
                  const std::shared_ptr<Segment>& segment,
                  const std::shared_ptr<Option>& option,
                  const RequestGroup* requestGroup, const DownloadEngine* e,
                  const std::shared_ptr<Request>& proxyRequest,
                  int64_t endOffset)
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
  httpRequest->setAcceptMetalink(
      requestGroup->getDownloadContext()->getAcceptMetalink());
  httpRequest->setNoWantDigest(
      option->getAsBool(PREF_NO_WANT_DIGEST_HEADER));

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

namespace {
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
} // namespace

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

} // namespace aria2
