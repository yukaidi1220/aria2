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
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "HttpsServiceBindingCache.h"

#include <chrono>
#include <iterator>

#include "ServiceBindingSelector.h"
#include "wallclock.h"

namespace aria2 {

HttpsServiceBindingCache::Key
HttpsServiceBindingCache::makeKey(const std::string& hostname, uint16_t port)
{
  return std::make_pair(hostname, port);
}

HttpsServiceBindingCache::EndpointKey
HttpsServiceBindingCache::makeEndpointKey(
    const dns::ServiceBindingEndpoint& endpoint)
{
  return std::make_tuple(endpoint.originHost, endpoint.originPort,
                         endpoint.connectHost, endpoint.connectPort,
                         endpoint.alpn);
}

bool HttpsServiceBindingCache::expired(const CacheEntry& entry)
{
  return entry.expiry <= global::wallclock();
}

HttpsServiceBindingCache::HttpsServiceBindingCache() = default;

HttpsServiceBindingCache::HttpsServiceBindingCache(
    const HttpsServiceBindingCache& c) = default;

HttpsServiceBindingCache::~HttpsServiceBindingCache() = default;

HttpsServiceBindingCache& HttpsServiceBindingCache::operator=(
    const HttpsServiceBindingCache& c)
{
  if (this != &c) {
    entries_ = c.entries_;
    resolving_ = c.resolving_;
    endpointFailures_ = c.endpointFailures_;
  }
  return *this;
}

void HttpsServiceBindingCache::cache(
    const std::string& hostname, uint16_t port,
    const std::vector<dns::ServiceBindingRecord>& records, uint32_t ttl)
{
  auto key = makeKey(hostname, port);
  if (ttl == 0) {
    entries_.erase(key);
    return;
  }

  CacheEntry entry;
  entry.records = records;
  entry.expiry = global::wallclock();
  entry.expiry.advance(std::chrono::seconds(ttl));
  entries_[key] = entry;
}

const std::vector<dns::ServiceBindingRecord>*
HttpsServiceBindingCache::find(const std::string& hostname, uint16_t port)
{
  auto key = makeKey(hostname, port);
  auto i = entries_.find(key);
  if (i == entries_.end()) {
    return nullptr;
  }
  if (expired((*i).second)) {
    entries_.erase(i);
    return nullptr;
  }
  return &(*i).second.records;
}

void HttpsServiceBindingCache::remove(const std::string& hostname,
                                      uint16_t port)
{
  entries_.erase(makeKey(hostname, port));
}

bool HttpsServiceBindingCache::markResolving(const std::string& hostname,
                                             uint16_t port)
{
  return resolving_.insert(makeKey(hostname, port)).second;
}

bool HttpsServiceBindingCache::isResolving(const std::string& hostname,
                                           uint16_t port) const
{
  return resolving_.count(makeKey(hostname, port)) != 0;
}

void HttpsServiceBindingCache::finishResolving(const std::string& hostname,
                                               uint16_t port)
{
  resolving_.erase(makeKey(hostname, port));
}

void HttpsServiceBindingCache::markEndpointFailed(
    const dns::ServiceBindingEndpoint& endpoint, uint32_t ttl)
{
  auto key = makeEndpointKey(endpoint);
  if (ttl == 0) {
    endpointFailures_.erase(key);
    return;
  }

  Timer expiry = global::wallclock();
  expiry.advance(std::chrono::seconds(ttl));
  endpointFailures_[key] = expiry;
}

bool HttpsServiceBindingCache::isEndpointFailed(
    const dns::ServiceBindingEndpoint& endpoint)
{
  auto key = makeEndpointKey(endpoint);
  auto i = endpointFailures_.find(key);
  if (i == std::end(endpointFailures_)) {
    return false;
  }
  if ((*i).second <= global::wallclock()) {
    endpointFailures_.erase(i);
    return false;
  }
  return true;
}

void HttpsServiceBindingCache::clearEndpointFailure(
    const dns::ServiceBindingEndpoint& endpoint)
{
  endpointFailures_.erase(makeEndpointKey(endpoint));
}

} // namespace aria2
