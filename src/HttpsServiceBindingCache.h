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
#ifndef D_HTTPS_SERVICE_BINDING_CACHE_H
#define D_HTTPS_SERVICE_BINDING_CACHE_H

#include "common.h"

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DnsMessage.h"
#include "TimerA2.h"

namespace aria2 {

class HttpsServiceBindingCache {
private:
  typedef std::pair<std::string, uint16_t> Key;

  struct CacheEntry {
    std::vector<dns::ServiceBindingRecord> records;
    Timer expiry;
  };

  typedef std::map<Key, CacheEntry> CacheEntryMap;

  CacheEntryMap entries_;
  std::set<Key> resolving_;

  static Key makeKey(const std::string& hostname, uint16_t port);

  static bool expired(const CacheEntry& entry);

public:
  HttpsServiceBindingCache();
  HttpsServiceBindingCache(const HttpsServiceBindingCache& c);
  ~HttpsServiceBindingCache();

  HttpsServiceBindingCache& operator=(const HttpsServiceBindingCache& c);

  void cache(const std::string& hostname, uint16_t port,
             const std::vector<dns::ServiceBindingRecord>& records,
             uint32_t ttl);

  const std::vector<dns::ServiceBindingRecord>*
  find(const std::string& hostname, uint16_t port);

  void remove(const std::string& hostname, uint16_t port);

  bool markResolving(const std::string& hostname, uint16_t port);

  bool isResolving(const std::string& hostname, uint16_t port) const;

  void finishResolving(const std::string& hostname, uint16_t port);
};

} // namespace aria2

#endif // D_HTTPS_SERVICE_BINDING_CACHE_H
