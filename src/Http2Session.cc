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
#  include <iterator>
#  include <vector>

#  include <nghttp2/nghttp2.h>

#  include "DlAbortEx.h"
#  include "fmt.h"

namespace aria2 {

namespace {
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

void checkNghttp2Result(int rv, const char* context)
{
  if (rv < 0) {
    throw DL_ABORT_EX(fmt("%s failed: %s", context, nghttp2_strerror(rv)));
  }
}
} // namespace

struct Http2Session::Impl {
  nghttp2_session* session = nullptr;
  std::string outbound;
  bool sendFailed = false;

  Impl()
  {
    nghttp2_session_callbacks* callbacks = nullptr;
    checkNghttp2Result(nghttp2_session_callbacks_new(&callbacks),
                       "nghttp2_session_callbacks_new");
    nghttp2_session_callbacks_set_send_callback(callbacks, sendCallback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(
        callbacks, noopOnFrameRecvCallback);
    nghttp2_session_callbacks_set_on_header_callback(
        callbacks, noopOnHeaderCallback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, noopOnDataChunkRecvCallback);
    nghttp2_session_callbacks_set_on_stream_close_callback(
        callbacks, noopOnStreamCloseCallback);
    auto rv = nghttp2_session_client_new(&session, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    checkNghttp2Result(rv, "nghttp2_session_client_new");

    checkNghttp2Result(
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0),
        "nghttp2_submit_settings");
  }

  ~Impl() { nghttp2_session_del(session); }

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

  static int noopOnFrameRecvCallback(nghttp2_session* session,
                                     const nghttp2_frame* frame,
                                     void* userData)
  {
    (void)session;
    (void)frame;
    (void)userData;
    return 0;
  }

  static int noopOnHeaderCallback(nghttp2_session* session,
                                  const nghttp2_frame* frame,
                                  const uint8_t* name, size_t namelen,
                                  const uint8_t* value, size_t valuelen,
                                  uint8_t flags, void* userData)
  {
    (void)session;
    (void)frame;
    (void)name;
    (void)namelen;
    (void)value;
    (void)valuelen;
    (void)flags;
    (void)userData;
    return 0;
  }

  static int noopOnDataChunkRecvCallback(nghttp2_session* session,
                                         uint8_t flags, int32_t streamId,
                                         const uint8_t* data, size_t len,
                                         void* userData)
  {
    (void)session;
    (void)flags;
    (void)streamId;
    (void)data;
    (void)len;
    (void)userData;
    return 0;
  }

  static int noopOnStreamCloseCallback(nghttp2_session* session,
                                       int32_t streamId, uint32_t errorCode,
                                       void* userData)
  {
    (void)session;
    (void)streamId;
    (void)errorCode;
    (void)userData;
    return 0;
  }
};

Http2Session::Http2Session() : impl_(new Impl()) {}

Http2Session::~Http2Session() { delete impl_; }

int32_t Http2Session::submitRequestHeaders(const Http2HeaderBlock& headers)
{
  std::vector<nghttp2_nv> nva;
  nva.reserve(headers.size());
  std::transform(std::begin(headers), std::end(headers),
                 std::back_inserter(nva), makeNV);

  auto streamId =
      nghttp2_submit_request(impl_->session, nullptr, nva.data(), nva.size(),
                             nullptr, nullptr);
  checkNghttp2Result(streamId, "nghttp2_submit_request");
  impl_->sendFailed = false;
  checkNghttp2Result(nghttp2_session_send(impl_->session),
                     "nghttp2_session_send");
  if (impl_->sendFailed) {
    throw DL_ABORT_EX("nghttp2 send callback failed");
  }
  return streamId;
}

std::string Http2Session::drainOutboundData()
{
  std::string data;
  data.swap(impl_->outbound);
  return data;
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
