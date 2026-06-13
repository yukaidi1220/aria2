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
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version of the file(s).  If you delete this exception statement from
 * all source files in the program, then also delete it here.
 */
/* copyright --> */
#include "Http2Session.h"

#ifdef HAVE_LIBNGHTTP2

#  include <algorithm>
#  include <cstring>
#  include <iterator>
#  include <list>
#  include <map>
#  include <memory>
#  include <utility>
#  include <vector>

#  include <nghttp2/nghttp2.h>

#  include "a2functional.h"
#  include "DlAbortEx.h"
#  include "fmt.h"
#  include "util.h"

namespace aria2 {

namespace {
const int32_t HTTP2_LOCAL_STREAM_WINDOW_SIZE = static_cast<int32_t>(256_m);
const int32_t HTTP2_LOCAL_CONNECTION_WINDOW_SIZE = static_cast<int32_t>(64_m);

nghttp2_nv makeNV(const Http2Header& header)
{
  nghttp2_nv nv;
  nv.name =
      reinterpret_cast<uint8_t*>(const_cast<char*>(header.name.data()));
  nv.namelen = header.name.size();
  nv.value =
      reinterpret_cast<uint8_t*>(const_cast<char*>(header.value.data()));
  nv.valuelen = header.value.size();
  nv.flags = NGHTTP2_NV_FLAG_NONE;
  return nv;
}

void checkNghttp2Result(ssize_t rv, const char* context)
{
  if (rv < 0) {
    throw DL_ABORT_EX(
        fmt("%s failed: %s", context, nghttp2_strerror(static_cast<int>(rv))));
  }
}
} // namespace

struct Http2RequestBodySource {
  explicit Http2RequestBodySource(std::string body)
      : body(std::move(body)), offset(0), streamId(0)
  {
  }

  std::string body;
  size_t offset;
  int32_t streamId;
};

struct Http2Session::Impl {
  nghttp2_session* session = nullptr;
  std::string outbound;
  bool sendFailed = false;
  bool callbackFailed = false;
  std::string callbackError;
  std::map<int32_t, Http2ResponseEvent> responses;
  std::list<Http2RequestBodySource> requestBodies;

  Impl()
  {
    nghttp2_session_callbacks* callbacks = nullptr;
    checkNghttp2Result(nghttp2_session_callbacks_new(&callbacks),
                       "nghttp2_session_callbacks_new");
    nghttp2_session_callbacks_set_send_callback(callbacks, sendCallback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(
        callbacks, onFrameRecvCallback);
    nghttp2_session_callbacks_set_on_header_callback(
        callbacks, onHeaderCallback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, onDataChunkRecvCallback);
    nghttp2_session_callbacks_set_on_stream_close_callback(
        callbacks, onStreamCloseCallback);
    auto rv = nghttp2_session_client_new(&session, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    checkNghttp2Result(rv, "nghttp2_session_client_new");

    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
         static_cast<uint32_t>(HTTP2_LOCAL_STREAM_WINDOW_SIZE)}};
    checkNghttp2Result(
        nghttp2_submit_settings(
            session, NGHTTP2_FLAG_NONE, settings,
            sizeof(settings) / sizeof(settings[0])),
        "nghttp2_submit_settings");
    checkNghttp2Result(
        nghttp2_session_set_local_window_size(
            session, NGHTTP2_FLAG_NONE, 0,
            HTTP2_LOCAL_CONNECTION_WINDOW_SIZE),
        "nghttp2_session_set_local_window_size");
  }

  ~Impl() { nghttp2_session_del(session); }

  void setCallbackFailure(const char* error)
  {
    callbackFailed = true;
    if (callbackError.empty()) {
      callbackError = error;
    }
  }

  static bool isResponseHeaders(const nghttp2_frame* frame)
  {
    return frame->hd.type == NGHTTP2_HEADERS &&
           frame->headers.cat == NGHTTP2_HCAT_RESPONSE;
  }

  static bool isInformationalResponse(const Http2ResponseEvent& response)
  {
    return response.status >= 100 && response.status < 200;
  }

  static void clearInformationalResponse(Http2ResponseEvent& response)
  {
    response.status = 0;
    response.headers.clear();
    response.headersComplete = false;
  }

  Http2ResponseEvent& getResponse(int32_t streamId)
  {
    auto& response = responses[streamId];
    response.streamId = streamId;
    return response;
  }

  void releaseRequestBody(int32_t streamId)
  {
    requestBodies.remove_if([streamId](const Http2RequestBodySource& source) {
      return source.streamId == streamId;
    });
  }

  void sendPendingData(const char* context)
  {
    sendFailed = false;
    auto rv = nghttp2_session_send(session);
    if (sendFailed) {
      throw DL_ABORT_EX("nghttp2 send callback failed");
    }
    checkNghttp2Result(rv, context);
  }

  static ssize_t sendCallback(nghttp2_session* session, const uint8_t* data,
                              size_t length, int flags, void* userData)
  {
    (void)session;
    (void)flags;
    auto impl = static_cast<Impl*>(userData);
    try {
      impl->outbound.append(reinterpret_cast<const char*>(data), length);
    }
    catch (...) {
      impl->sendFailed = true;
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return static_cast<ssize_t>(length);
  }

  static ssize_t readCallback(nghttp2_session* session, int32_t streamId,
                              uint8_t* buf, size_t length,
                              uint32_t* dataFlags,
                              nghttp2_data_source* source, void* userData)
  {
    (void)session;
    (void)streamId;
    (void)userData;
    auto body = static_cast<Http2RequestBodySource*>(source->ptr);
    auto remaining = body->body.size() - body->offset;
    auto chunkLength = std::min(length, remaining);
    if (chunkLength > 0) {
      std::memcpy(buf, body->body.data() + body->offset, chunkLength);
      body->offset += chunkLength;
    }
    if (body->offset == body->body.size()) {
      *dataFlags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return static_cast<ssize_t>(chunkLength);
  }

  static int onFrameRecvCallback(nghttp2_session* session,
                                 const nghttp2_frame* frame, void* userData)
  {
    (void)session;
    auto impl = static_cast<Impl*>(userData);
    try {
      if (isResponseHeaders(frame)) {
        auto& response = impl->getResponse(frame->hd.stream_id);
        if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
          if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) &&
              isInformationalResponse(response)) {
            clearInformationalResponse(response);
            return 0;
          }
          response.headersComplete = true;
        }
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
          response.streamClosed = true;
          response.errorCode = NGHTTP2_NO_ERROR;
          response.body.close(NGHTTP2_NO_ERROR);
        }
      }
    }
    catch (...) {
      impl->setCallbackFailure("nghttp2 frame callback failed");
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
  }

  static int onHeaderCallback(nghttp2_session* session,
                              const nghttp2_frame* frame, const uint8_t* name,
                              size_t namelen, const uint8_t* value,
                              size_t valuelen, uint8_t flags, void* userData)
  {
    (void)session;
    (void)flags;
    auto impl = static_cast<Impl*>(userData);
    try {
      if (!isResponseHeaders(frame)) {
        return 0;
      }

      std::string nameString(reinterpret_cast<const char*>(name), namelen);
      std::string valueString(reinterpret_cast<const char*>(value), valuelen);
      auto& response = impl->getResponse(frame->hd.stream_id);
      if (nameString == ":status") {
        int32_t status = 0;
        if (util::parseIntNoThrow(status, valueString) && status >= 100 &&
            status <= 999) {
          response.status = status;
        }
        return 0;
      }
      response.headers.emplace_back(std::move(nameString),
                                    std::move(valueString));
    }
    catch (...) {
      impl->setCallbackFailure("nghttp2 header callback failed");
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
  }

  static int onDataChunkRecvCallback(nghttp2_session* session, uint8_t flags,
                                     int32_t streamId, const uint8_t* data,
                                     size_t len, void* userData)
  {
    (void)session;
    (void)flags;
    auto impl = static_cast<Impl*>(userData);
    try {
      auto& response = impl->getResponse(streamId);
      if (!response.body.push(data, len)) {
        impl->setCallbackFailure("HTTP/2 response body queue is full");
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      }
    }
    catch (...) {
      impl->setCallbackFailure("nghttp2 data callback failed");
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
  }

  static int onStreamCloseCallback(nghttp2_session* session, int32_t streamId,
                                   uint32_t errorCode, void* userData)
  {
    (void)session;
    auto impl = static_cast<Impl*>(userData);
    try {
      auto& response = impl->getResponse(streamId);
      response.streamClosed = true;
      response.errorCode = errorCode;
      response.body.close(errorCode);
      impl->releaseRequestBody(streamId);
    }
    catch (...) {
      impl->setCallbackFailure("nghttp2 stream close callback failed");
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
  }
};

Http2Session::Http2Session() : impl_(new Impl()) {}

Http2Session::~Http2Session() { delete impl_; }

int32_t Http2Session::submitRequestHeaders(const Http2HeaderBlock& headers)
{
  return submitRequest(headers, std::string());
}

int32_t Http2Session::submitRequest(const Http2HeaderBlock& headers,
                                    const std::string& body)
{
  std::vector<nghttp2_nv> nva;
  nva.reserve(headers.size());
  std::transform(std::begin(headers), std::end(headers),
                 std::back_inserter(nva), makeNV);

  nghttp2_data_provider dataProvider;
  nghttp2_data_provider* dataProviderPtr = nullptr;
  if (!body.empty()) {
    impl_->requestBodies.emplace_back(body);
    dataProvider.source.ptr = &impl_->requestBodies.back();
    dataProvider.read_callback = Impl::readCallback;
    dataProviderPtr = &dataProvider;
  }

  auto streamId =
      nghttp2_submit_request(impl_->session, nullptr, nva.data(), nva.size(),
                             dataProviderPtr, nullptr);
  if (streamId < 0 && dataProviderPtr) {
    impl_->requestBodies.pop_back();
  }
  checkNghttp2Result(streamId, "nghttp2_submit_request");
  if (dataProviderPtr) {
    impl_->requestBodies.back().streamId = streamId;
  }
  impl_->sendPendingData("nghttp2_session_send");
  return streamId;
}

std::string Http2Session::drainOutboundData()
{
  std::string data;
  data.swap(impl_->outbound);
  return data;
}

void Http2Session::feedInboundData(const std::string& data)
{
  impl_->callbackFailed = false;
  impl_->callbackError.clear();
  auto rv = nghttp2_session_mem_recv(
      impl_->session, reinterpret_cast<const uint8_t*>(data.data()),
      data.size());
  if (impl_->callbackFailed) {
    throw DL_ABORT_EX(impl_->callbackError.empty()
                          ? "nghttp2 receive callback failed"
                          : impl_->callbackError.c_str());
  }
  checkNghttp2Result(rv, "nghttp2_session_mem_recv");
  impl_->sendPendingData("nghttp2_session_send");
}

void Http2Session::resetStream(int32_t streamId)
{
  checkNghttp2Result(
      nghttp2_submit_rst_stream(impl_->session, NGHTTP2_FLAG_NONE, streamId,
                                NGHTTP2_CANCEL),
      "nghttp2_submit_rst_stream");
  impl_->releaseRequestBody(streamId);
  impl_->sendPendingData("nghttp2_session_send");
}

size_t Http2Session::getRemoteMaxConcurrentStreams() const
{
  return nghttp2_session_get_remote_settings(
      impl_->session, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
}

bool Http2Session::hasResponseBodySpace(size_t len) const
{
  for (const auto& entry : impl_->responses) {
    const auto& body = entry.second.body;
    if (!body.closed() && body.available() < len) {
      return false;
    }
  }
  return true;
}

bool Http2Session::hasResponseEvent(int32_t streamId) const
{
  return impl_->responses.find(streamId) != impl_->responses.end();
}

const Http2ResponseEvent* Http2Session::findResponseEvent(
    int32_t streamId) const
{
  auto itr = impl_->responses.find(streamId);
  if (itr == impl_->responses.end()) {
    return nullptr;
  }
  return &itr->second;
}

std::string Http2Session::popResponseBody(int32_t streamId, size_t maxLen)
{
  auto itr = impl_->responses.find(streamId);
  if (itr == impl_->responses.end()) {
    return std::string();
  }
  return itr->second.body.pop(maxLen);
}

std::unique_ptr<Http2ResponseEvent>
Http2Session::popResponseEvent(int32_t streamId)
{
  auto itr = impl_->responses.find(streamId);
  if (itr == impl_->responses.end()) {
    return nullptr;
  }

  auto event = make_unique<Http2ResponseEvent>(std::move(itr->second));
  impl_->responses.erase(itr);
  return event;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
