#ifndef D_HTTP2_TEST_UTIL_H
#define D_HTTP2_TEST_UTIL_H

#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Session.h"

#  include <algorithm>
#  include <cstring>
#  include <iterator>
#  include <string>
#  include <vector>

#  include <cppunit/extensions/HelperMacros.h>
#  include <nghttp2/nghttp2.h>

namespace aria2 {
namespace http2test {

inline Http2HeaderBlock createRequestHeaders()
{
  Http2HeaderBlock headers;
  headers.emplace_back(":method", "GET");
  headers.emplace_back(":scheme", "https");
  headers.emplace_back(":authority", "example.org");
  headers.emplace_back(":path", "/file");
  headers.emplace_back("user-agent", "aria2");
  return headers;
}

inline bool containsFrameType(const std::string& data, unsigned char type)
{
  size_t offset = 24;
  while (offset + 9 <= data.size()) {
    auto length = (static_cast<unsigned char>(data[offset]) << 16) |
                  (static_cast<unsigned char>(data[offset + 1]) << 8) |
                  static_cast<unsigned char>(data[offset + 2]);
    if (static_cast<unsigned char>(data[offset + 3]) == type) {
      return true;
    }
    offset += 9 + length;
  }
  return false;
}

inline bool containsSettingsAck(const std::string& data)
{
  size_t offset = 0;
  if (data.size() >= 24 &&
      data.substr(0, 24) == "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") {
    offset = 24;
  }
  while (offset + 9 <= data.size()) {
    auto length = (static_cast<unsigned char>(data[offset]) << 16) |
                  (static_cast<unsigned char>(data[offset + 1]) << 8) |
                  static_cast<unsigned char>(data[offset + 2]);
    auto type = static_cast<unsigned char>(data[offset + 3]);
    auto flags = static_cast<unsigned char>(data[offset + 4]);
    if (length == 0 && type == NGHTTP2_SETTINGS &&
        (flags & NGHTTP2_FLAG_ACK)) {
      return true;
    }
    offset += 9 + length;
  }
  return false;
}

inline nghttp2_nv makeNV(const Http2Header& header)
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

inline void assertNghttp2Success(ssize_t rv)
{
  CPPUNIT_ASSERT(rv >= 0);
}

class FakeHttp2ServerSession {
private:
  nghttp2_session* session_ = nullptr;
  std::string outbound_;
  std::string body_;
  size_t bodyOffset_ = 0;
  bool callbackFailed_ = false;

public:
  FakeHttp2ServerSession()
  {
    nghttp2_session_callbacks* callbacks = nullptr;
    assertNghttp2Success(nghttp2_session_callbacks_new(&callbacks));
    nghttp2_session_callbacks_set_send_callback(callbacks, sendCallback);
    auto rv = nghttp2_session_server_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    assertNghttp2Success(rv);

    assertNghttp2Success(
        nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, nullptr, 0));
  }

  ~FakeHttp2ServerSession() { nghttp2_session_del(session_); }

  void feedInboundData(const std::string& data)
  {
    assertNghttp2Success(nghttp2_session_mem_recv(
        session_, reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    CPPUNIT_ASSERT(!callbackFailed_);
  }

  void submitResponse(int32_t streamId, const Http2HeaderBlock& headers,
                      const std::string& body)
  {
    std::vector<nghttp2_nv> nva;
    nva.reserve(headers.size());
    std::transform(std::begin(headers), std::end(headers),
                   std::back_inserter(nva), makeNV);

    body_ = body;
    bodyOffset_ = 0;
    nghttp2_data_provider dataProvider;
    dataProvider.source.ptr = this;
    dataProvider.read_callback = readCallback;
    assertNghttp2Success(nghttp2_submit_response(
        session_, streamId, nva.data(), nva.size(), &dataProvider));
    assertNghttp2Success(nghttp2_session_send(session_));
    CPPUNIT_ASSERT(!callbackFailed_);
  }

  void submitResponseHeaders(int32_t streamId,
                             const Http2HeaderBlock& headers)
  {
    std::vector<nghttp2_nv> nva;
    nva.reserve(headers.size());
    std::transform(std::begin(headers), std::end(headers),
                   std::back_inserter(nva), makeNV);

    assertNghttp2Success(nghttp2_submit_headers(
        session_, NGHTTP2_FLAG_NONE, streamId, nullptr, nva.data(),
        nva.size(), nullptr));
    assertNghttp2Success(nghttp2_session_send(session_));
    CPPUNIT_ASSERT(!callbackFailed_);
  }

  std::string drainOutboundData()
  {
    std::string data;
    data.swap(outbound_);
    return data;
  }

  void flushPendingData()
  {
    assertNghttp2Success(nghttp2_session_send(session_));
    CPPUNIT_ASSERT(!callbackFailed_);
  }

private:
  static ssize_t sendCallback(nghttp2_session* session, const uint8_t* data,
                              size_t length, int flags, void* userData)
  {
    (void)session;
    (void)flags;
    auto server = static_cast<FakeHttp2ServerSession*>(userData);
    try {
      server->outbound_.append(reinterpret_cast<const char*>(data), length);
    }
    catch (...) {
      server->callbackFailed_ = true;
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
    auto server = static_cast<FakeHttp2ServerSession*>(source->ptr);
    auto remaining = server->body_.size() - server->bodyOffset_;
    auto chunkLength = std::min(length, remaining);
    if (chunkLength > 0) {
      std::memcpy(buf, server->body_.data() + server->bodyOffset_,
                  chunkLength);
      server->bodyOffset_ += chunkLength;
    }
    if (server->bodyOffset_ == server->body_.size()) {
      *dataFlags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return static_cast<ssize_t>(chunkLength);
  }
};

inline std::string findHeader(const Http2HeaderBlock& headers,
                              const std::string& name)
{
  for (const auto& header : headers) {
    if (header.name == name) {
      return header.value;
    }
  }
  return "";
}

inline Http2HeaderBlock createResponseHeaders()
{
  Http2HeaderBlock headers;
  headers.emplace_back(":status", "200");
  headers.emplace_back("content-type", "text/plain");
  headers.emplace_back("x-test", "ok");
  return headers;
}

inline void feedInChunks(Http2Session& session, const std::string& data)
{
  size_t offset = 0;
  size_t chunkSize = 1;
  while (offset < data.size()) {
    auto len = std::min(chunkSize, data.size() - offset);
    session.feedInboundData(data.substr(offset, len));
    offset += len;
    chunkSize = std::min(chunkSize + 2, (size_t)11);
  }
}

} // namespace http2test
} // namespace aria2

#endif // HAVE_LIBNGHTTP2

#endif // D_HTTP2_TEST_UTIL_H
