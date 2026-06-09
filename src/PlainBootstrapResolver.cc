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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "PlainBootstrapResolver.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace aria2 {

PlainBootstrapResolver::PlainBootstrapResolver(
    int family, std::vector<std::shared_ptr<AsyncResolver>> resolvers)
    : family_(family), resolvers_(std::move(resolvers))
{
}

void PlainBootstrapResolver::updateSockets()
{
  socks_.clear();
  for (const auto& resolver : resolvers_) {
    const auto& entries = resolver->getsock();
    socks_.insert(std::end(socks_), std::begin(entries), std::end(entries));
  }
}

void PlainBootstrapResolver::updateResolvedAddresses()
{
  resolvedAddresses_.clear();
  for (const auto& resolver : resolvers_) {
    if (resolver->getStatus() == STATUS_SUCCESS) {
      const auto& addrs = resolver->getResolvedAddresses();
      resolvedAddresses_.insert(std::end(resolvedAddresses_), std::begin(addrs),
                                std::end(addrs));
    }
  }
}

void PlainBootstrapResolver::updateError()
{
  error_.clear();
  for (const auto& resolver : resolvers_) {
    if (resolver->getStatus() == STATUS_ERROR && !resolver->getError().empty()) {
      error_ = resolver->getError();
    }
  }
}

bool PlainBootstrapResolver::socketBelongsTo(const AsyncResolver* resolver,
                                             sock_t fd) const
{
  if (fd == badSocket()) {
    return false;
  }
  const auto& entries = resolver->getsock();
  return std::find_if(std::begin(entries), std::end(entries),
                      [fd](const AsyncResolverSocketEntry& entry) {
                        return entry.fd == fd;
                      }) != std::end(entries);
}

void PlainBootstrapResolver::resolve(const std::string& name)
{
  hostname_ = name;
  error_.clear();
  resolvedAddresses_.clear();
  for (const auto& resolver : resolvers_) {
    resolver->resolve(name);
  }
  updateResolvedAddresses();
  updateError();
  updateSockets();
}

const std::vector<std::string>&
PlainBootstrapResolver::getResolvedAddresses() const
{
  return resolvedAddresses_;
}

const std::string& PlainBootstrapResolver::getError() const { return error_; }

AsyncResolver::STATUS PlainBootstrapResolver::getStatus() const
{
  size_t error = 0;
  for (const auto& resolver : resolvers_) {
    if (resolver->getStatus() == STATUS_SUCCESS) {
      return STATUS_SUCCESS;
    }
    if (resolver->getStatus() == STATUS_ERROR) {
      ++error;
    }
  }
  if (resolvers_.empty() || error == resolvers_.size()) {
    return STATUS_ERROR;
  }
  return STATUS_QUERYING;
}

bool PlainBootstrapResolver::usable() const
{
  for (const auto& resolver : resolvers_) {
    if ((resolver->getStatus() == STATUS_READY ||
         resolver->getStatus() == STATUS_QUERYING) &&
        resolver->usable()) {
      return true;
    }
  }
  return false;
}

int PlainBootstrapResolver::getFamily() const { return family_; }

const std::vector<AsyncResolverSocketEntry>& PlainBootstrapResolver::getsock()
    const
{
  return socks_;
}

void PlainBootstrapResolver::process(sock_t readfd, sock_t writefd)
{
  if (readfd == badSocket() && writefd == badSocket()) {
    for (const auto& resolver : resolvers_) {
      resolver->process(readfd, writefd);
    }
    updateResolvedAddresses();
    updateError();
    updateSockets();
    return;
  }

  for (const auto& resolver : resolvers_) {
    auto childReadfd =
        socketBelongsTo(resolver.get(), readfd) ? readfd : badSocket();
    auto childWritefd =
        socketBelongsTo(resolver.get(), writefd) ? writefd : badSocket();
    if (childReadfd == badSocket() && childWritefd == badSocket()) {
      continue;
    }
    resolver->process(childReadfd, childWritefd);
  }
  updateResolvedAddresses();
  updateError();
  updateSockets();
}

void PlainBootstrapResolver::processTimeout()
{
  for (const auto& resolver : resolvers_) {
    resolver->processTimeout();
  }
  updateResolvedAddresses();
  updateError();
  updateSockets();
}

const std::string& PlainBootstrapResolver::getHostname() const
{
  return hostname_;
}

} // namespace aria2
