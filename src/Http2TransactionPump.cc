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
#include "Http2TransactionPump.h"

#ifdef HAVE_LIBNGHTTP2

#  include <algorithm>

#  include "DlAbortEx.h"
#  include "Http2Connection.h"
#  include "Http2Transaction.h"
#  include "Http2Transport.h"
#  include "a2functional.h"

namespace aria2 {

namespace {
const size_t MAX_HTTP2_TRANSPORT_READ_SIZE = 64_k;
const size_t MAX_HTTP2_TRANSPORT_READ_ITERATIONS = 64;
} // namespace

Http2TransactionPump::Http2TransactionPump(Http2Transaction& transaction,
                                           Http2Transport& transport)
    : Http2TransactionPump(transaction.getConnection(), transport)
{
}

Http2TransactionPump::Http2TransactionPump(Http2Connection& connection,
                                           Http2Transport& transport)
    : connection_(connection), transport_(transport), writeOffset_(0),
      outboundDataPending_(false)
{
}

Http2TransactionPump::~Http2TransactionPump() = default;

void Http2TransactionPump::appendOutboundData()
{
  clearSentOutboundData();

  auto data = connection_.drainOutboundData();
  outboundDataPending_ = false;
  if (!data.empty()) {
    writeBuffer_.append(data);
  }
}

void Http2TransactionPump::clearSentOutboundData()
{
  if (writeOffset_ == writeBuffer_.size()) {
    writeBuffer_.clear();
    writeOffset_ = 0;
  }
}

bool Http2TransactionPump::canReadInboundData() const
{
  return connection_.hasResponseBodySpace(MAX_HTTP2_TRANSPORT_READ_SIZE);
}

bool Http2TransactionPump::flushOutboundData()
{
  appendOutboundData();

  bool progressed = false;
  while (writeOffset_ < writeBuffer_.size()) {
    auto remaining = writeBuffer_.size() - writeOffset_;
    auto nwrite =
        transport_.writeData(writeBuffer_.data() + writeOffset_, remaining);
    if (nwrite < 0) {
      throw DL_ABORT_EX("HTTP/2 transport write failed");
    }
    if (nwrite == 0) {
      if (!transport_.wantRead() && !transport_.wantWrite()) {
        throw DL_ABORT_EX("HTTP/2 transport closed while writing");
      }
      break;
    }
    if (static_cast<size_t>(nwrite) > remaining) {
      throw DL_ABORT_EX("HTTP/2 transport wrote more data than requested");
    }
    writeOffset_ += static_cast<size_t>(nwrite);
    progressed = true;
  }

  clearSentOutboundData();
  return progressed;
}

bool Http2TransactionPump::readInboundData()
{
  if (!canReadInboundData()) {
    return false;
  }

  unsigned char buf[MAX_HTTP2_TRANSPORT_READ_SIZE];
  auto nread = transport_.readData(buf, sizeof(buf));
  if (nread < 0) {
    throw DL_ABORT_EX("HTTP/2 transport read failed");
  }
  if (nread == 0) {
    if (!transport_.wantRead() && !transport_.wantWrite()) {
      throw DL_ABORT_EX("HTTP/2 transport closed while reading");
    }
    return false;
  }
  if (static_cast<size_t>(nread) > sizeof(buf)) {
    throw DL_ABORT_EX("HTTP/2 transport read more data than requested");
  }

  connection_.feedInboundData(
      std::string(reinterpret_cast<const char*>(buf),
                  static_cast<size_t>(nread)));
  outboundDataPending_ = true;
  return true;
}

void Http2TransactionPump::notifyPendingOutboundData()
{
  outboundDataPending_ = true;
}

bool Http2TransactionPump::pump()
{
  bool progressed = false;
  progressed |= flushOutboundData();
  if (readInboundData()) {
    progressed = true;
    progressed |= flushOutboundData();
  }
  for (size_t i = 1; i < MAX_HTTP2_TRANSPORT_READ_ITERATIONS &&
                     transport_.getRecvBufferedLength() > 0 &&
                     canReadInboundData();
       ++i) {
    if (!readInboundData()) {
      break;
    }
    progressed = true;
    progressed |= flushOutboundData();
  }
  return progressed;
}

bool Http2TransactionPump::hasPendingOutboundData() const
{
  return writeOffset_ < writeBuffer_.size();
}

bool Http2TransactionPump::hasBufferedInboundData() const
{
  return transport_.getRecvBufferedLength() > 0 && canReadInboundData();
}

bool Http2TransactionPump::wantRead() const
{
  return canReadInboundData();
}

bool Http2TransactionPump::wantWrite() const
{
  return outboundDataPending_ || hasPendingOutboundData() ||
         transport_.wantWrite();
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
