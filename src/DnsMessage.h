/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_DNS_MESSAGE_H
#define D_DNS_MESSAGE_H

#include "common.h"

#include <string>
#include <vector>

namespace aria2 {

namespace dns {

enum QueryType {
  TYPE_A = 1,
  TYPE_AAAA = 28,
  TYPE_SVCB = 64,
  TYPE_HTTPS = 65,
};

struct SvcParam {
  uint16_t key = 0;
  std::string value;
};

struct ServiceBindingRecord {
  std::string ownerName;
  uint32_t ttl = 0;
  uint16_t priority = 0;
  std::string targetName;
  std::vector<uint16_t> mandatoryKeys;
  std::vector<uint16_t> paramKeys;
  std::vector<std::string> alpn;
  bool noDefaultAlpn = false;
  bool hasPort = false;
  uint16_t port = 0;
  std::vector<std::string> ipv4hint;
  std::vector<std::string> ipv6hint;
  std::string echConfigList;
  std::vector<SvcParam> unknownParams;
  bool aliasModeUnavailable = false;
  bool hasUnknownMandatoryKey = false;
};

std::string createQuery(uint16_t id, const std::string& hostname,
                        QueryType qtype);

std::vector<std::string> parseResponse(const unsigned char* data, size_t len,
                                       uint16_t expectedId,
                                       const std::string& expectedHostname,
                                       QueryType qtype);

std::vector<ServiceBindingRecord>
parseServiceBindingResponse(const unsigned char* data, size_t len,
                            uint16_t expectedId,
                            const std::string& expectedHostname,
                            QueryType qtype);

} // namespace dns

} // namespace aria2

#endif // D_DNS_MESSAGE_H
