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
#include "DnsMessage.h"

#include <set>
#include <utility>

#include "DlAbortEx.h"
#include "SocketCore.h"
#include "fmt.h"
#include "util.h"

namespace aria2 {

namespace dns {

namespace {

const uint16_t CLASS_IN = 1;
const uint16_t TYPE_CNAME = 5;
const size_t DNS_HEADER_SIZE = 12;
const size_t MAX_NAME_LENGTH = 255;

void appendUint16(std::string& out, uint16_t value)
{
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

uint16_t readUint16(const unsigned char* data, size_t len, size_t pos)
{
  if (pos + 2 > len) {
    throw DL_ABORT_EX("Truncated DNS message");
  }
  return (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
}

uint32_t readUint32(const unsigned char* data, size_t len, size_t pos)
{
  if (pos + 4 > len) {
    throw DL_ABORT_EX("Truncated DNS message");
  }
  return (static_cast<uint32_t>(data[pos]) << 24) |
         (static_cast<uint32_t>(data[pos + 1]) << 16) |
         (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
}

std::string normalizeName(std::string name)
{
  name = util::strip(std::move(name));
  if (name.empty()) {
    throw DL_ABORT_EX("DNS hostname is empty");
  }
  if (name[name.size() - 1] == '.') {
    name.resize(name.size() - 1);
  }
  if (name.empty() || name.size() > MAX_NAME_LENGTH) {
    throw DL_ABORT_EX("DNS hostname is invalid");
  }

  size_t wireLen = 1;
  size_t pos = 0;
  while (pos < name.size()) {
    auto dot = name.find('.', pos);
    auto end = dot == std::string::npos ? name.size() : dot;
    auto labelLen = end - pos;
    if (labelLen == 0 || labelLen > 63) {
      throw DL_ABORT_EX("DNS hostname label is invalid");
    }
    wireLen += 1 + labelLen;
    if (wireLen > MAX_NAME_LENGTH) {
      throw DL_ABORT_EX("DNS hostname is too long");
    }
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }
  return util::toLower(std::move(name));
}

void appendName(std::string& out, const std::string& hostname)
{
  auto name = normalizeName(hostname);
  size_t pos = 0;
  while (pos < name.size()) {
    auto dot = name.find('.', pos);
    auto end = dot == std::string::npos ? name.size() : dot;
    auto labelLen = end - pos;
    out.push_back(static_cast<char>(labelLen));
    out.append(name, pos, labelLen);
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }
  out.push_back(0);
}

std::string readName(const unsigned char* data, size_t len, size_t& pos,
                     std::set<size_t>& visited, size_t& wireLen)
{
  if (pos >= len) {
    throw DL_ABORT_EX("Truncated DNS name");
  }

  std::string name;
  for (;;) {
    if (pos >= len) {
      throw DL_ABORT_EX("Truncated DNS name");
    }
    auto pointerPos = pos;
    auto labelLen = data[pos];
    if ((labelLen & 0xc0) == 0xc0) {
      if (pos + 2 > len) {
        throw DL_ABORT_EX("Truncated DNS compression pointer");
      }
      auto ptr = ((static_cast<size_t>(labelLen) & 0x3f) << 8) |
                 static_cast<size_t>(data[pos + 1]);
      if (ptr >= pointerPos || ptr >= len || !visited.insert(ptr).second) {
        throw DL_ABORT_EX("Bad DNS compression pointer");
      }
      auto ptrPos = static_cast<size_t>(ptr);
      auto pointedName = readName(data, len, ptrPos, visited, wireLen);
      pos += 2;
      if (name.empty()) {
        return pointedName;
      }
      if (pointedName.empty()) {
        return name;
      }
      return name + "." + pointedName;
    }
    if ((labelLen & 0xc0) != 0) {
      throw DL_ABORT_EX("Bad DNS label");
    }
    ++pos;
    ++wireLen;
    if (wireLen > MAX_NAME_LENGTH) {
      throw DL_ABORT_EX("DNS name is too long");
    }
    if (labelLen == 0) {
      return name;
    }
    if (labelLen > 63 || pos + labelLen > len) {
      throw DL_ABORT_EX("Truncated DNS label");
    }
    if (!name.empty()) {
      name += '.';
    }
    name.append(reinterpret_cast<const char*>(data + pos), labelLen);
    wireLen += labelLen;
    if (wireLen > MAX_NAME_LENGTH) {
      throw DL_ABORT_EX("DNS name is too long");
    }
    pos += labelLen;
  }
}

std::string readName(const unsigned char* data, size_t len, size_t& pos)
{
  std::set<size_t> visited;
  size_t wireLen = 0;
  return util::toLower(readName(data, len, pos, visited, wireLen));
}

struct Question {
  std::string name;
  uint16_t type;
  uint16_t klass;
};

struct Answer {
  std::string ownerName;
  uint16_t type = 0;
  uint16_t klass = 0;
  size_t rdataPos = 0;
  uint16_t rdlength = 0;
  std::string cname;
};

Question readQuestion(const unsigned char* data, size_t len, size_t& pos)
{
  auto name = readName(data, len, pos);
  if (pos + 4 > len) {
    throw DL_ABORT_EX("Truncated DNS question");
  }
  Question question;
  question.name = std::move(name);
  question.type = readUint16(data, len, pos);
  question.klass = readUint16(data, len, pos + 2);
  pos += 4;
  return question;
}

std::string parseAddress(const unsigned char* data, size_t len, size_t pos,
                         uint16_t type, uint16_t rdlength)
{
  char buf[NI_MAXHOST];
  if (type == TYPE_A) {
    if (rdlength != 4 || pos + 4 > len) {
      throw DL_ABORT_EX("Bad DNS A record");
    }
    if (inetNtop(AF_INET, data + pos, buf, sizeof(buf)) != 0) {
      throw DL_ABORT_EX("Bad DNS A address");
    }
    return buf;
  }
  if (type == TYPE_AAAA) {
    if (rdlength != 16 || pos + 16 > len) {
      throw DL_ABORT_EX("Bad DNS AAAA record");
    }
    if (inetNtop(AF_INET6, data + pos, buf, sizeof(buf)) != 0) {
      throw DL_ABORT_EX("Bad DNS AAAA address");
    }
    return buf;
  }
  return std::string();
}

} // namespace

std::string createQuery(uint16_t id, const std::string& hostname,
                        QueryType qtype)
{
  std::string out;
  out.reserve(DNS_HEADER_SIZE + hostname.size() + 6);
  appendUint16(out, id);
  appendUint16(out, 0x0100); // RD
  appendUint16(out, 1);      // QDCOUNT
  appendUint16(out, 0);      // ANCOUNT
  appendUint16(out, 0);      // NSCOUNT
  appendUint16(out, 0);      // ARCOUNT
  appendName(out, hostname);
  appendUint16(out, static_cast<uint16_t>(qtype));
  appendUint16(out, CLASS_IN);
  return out;
}

std::vector<std::string> parseResponse(const unsigned char* data, size_t len,
                                       uint16_t expectedId,
                                       const std::string& expectedHostname,
                                       QueryType qtype)
{
  if (len < DNS_HEADER_SIZE) {
    throw DL_ABORT_EX("Truncated DNS message");
  }
  auto expectedName = normalizeName(expectedHostname);

  auto id = readUint16(data, len, 0);
  auto flags = readUint16(data, len, 2);
  auto qdcount = readUint16(data, len, 4);
  auto ancount = readUint16(data, len, 6);

  if (id != expectedId) {
    throw DL_ABORT_EX("DNS response ID mismatch");
  }
  if ((flags & 0x8000) == 0) {
    throw DL_ABORT_EX("DNS message is not a response");
  }
  if ((flags & 0x7800) != 0) {
    throw DL_ABORT_EX("Unexpected DNS opcode");
  }
  if ((flags & 0x0200) != 0) {
    throw DL_ABORT_EX("Truncated DNS response");
  }
  auto rcode = flags & 0x000f;
  if (rcode != 0) {
    throw DL_ABORT_EX(fmt("DNS response error code %u",
                          static_cast<unsigned int>(rcode)));
  }
  if (qdcount != 1) {
    throw DL_ABORT_EX("Unexpected DNS question count");
  }

  size_t pos = DNS_HEADER_SIZE;
  auto question = readQuestion(data, len, pos);
  if (question.name != expectedName ||
      question.type != static_cast<uint16_t>(qtype) ||
      question.klass != CLASS_IN) {
    throw DL_ABORT_EX("Unexpected DNS question");
  }

  std::set<std::string> acceptedNames;
  acceptedNames.insert(std::move(expectedName));

  std::vector<Answer> answers;
  for (size_t i = 0; i < ancount; ++i) {
    Answer answer;
    answer.ownerName = readName(data, len, pos);
    if (pos + 10 > len) {
      throw DL_ABORT_EX("Truncated DNS answer");
    }
    answer.type = readUint16(data, len, pos);
    answer.klass = readUint16(data, len, pos + 2);
    (void)readUint32(data, len, pos + 4); // TTL
    answer.rdlength = readUint16(data, len, pos + 8);
    pos += 10;
    if (pos + answer.rdlength > len) {
      throw DL_ABORT_EX("Truncated DNS RDATA");
    }
    answer.rdataPos = pos;
    if (answer.klass == CLASS_IN && answer.type == TYPE_CNAME) {
      auto rdataPos = pos;
      answer.cname = readName(data, pos + answer.rdlength, rdataPos);
      if (rdataPos != pos + answer.rdlength) {
        throw DL_ABORT_EX("Bad DNS CNAME record");
      }
    }
    auto rdlength = answer.rdlength;
    answers.push_back(std::move(answer));
    pos += rdlength;
  }

  bool addedName;
  do {
    addedName = false;
    for (const auto& answer : answers) {
      if (answer.klass == CLASS_IN && answer.type == TYPE_CNAME &&
          acceptedNames.find(answer.ownerName) != acceptedNames.end() &&
          !answer.cname.empty() &&
          acceptedNames.insert(answer.cname).second) {
        addedName = true;
      }
    }
  } while (addedName);

  std::vector<std::string> result;
  for (const auto& answer : answers) {
    if (answer.klass == CLASS_IN &&
        answer.type == static_cast<uint16_t>(qtype) &&
        acceptedNames.find(answer.ownerName) != acceptedNames.end()) {
      result.push_back(parseAddress(data, len, answer.rdataPos, answer.type,
                                    answer.rdlength));
    }
  }
  return result;
}

} // namespace dns

} // namespace aria2
