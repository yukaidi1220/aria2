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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class PlainBootstrapResolverTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(PlainBootstrapResolverTest);
  CPPUNIT_TEST(testResolveSucceedsWhenAnyChildSucceeds);
  CPPUNIT_TEST(testResolveFailsWhenAllChildrenFail);
  CPPUNIT_TEST(testProcessOnlyDispatchesMatchingSocket);
  CPPUNIT_TEST(testProcessEmptySocketDispatchesAllChildren);
  CPPUNIT_TEST(testProcessTimeoutDispatchesAllChildren);
  CPPUNIT_TEST_SUITE_END();

public:
  void testResolveSucceedsWhenAnyChildSucceeds();
  void testResolveFailsWhenAllChildrenFail();
  void testProcessOnlyDispatchesMatchingSocket();
  void testProcessEmptySocketDispatchesAllChildren();
  void testProcessTimeoutDispatchesAllChildren();
};

CPPUNIT_TEST_SUITE_REGISTRATION(PlainBootstrapResolverTest);

namespace {
class MockAsyncResolver : public AsyncResolver {
private:
  int family_;
  STATUS status_;
  std::vector<std::string> addrs_;
  std::string error_;
  std::string hostname_;
  std::vector<AsyncResolverSocketEntry> socks_;
  bool usable_;

public:
  size_t processCount = 0;
  size_t processTimeoutCount = 0;
  sock_t lastReadfd = badSocket();
  sock_t lastWritefd = badSocket();

  MockAsyncResolver(int family, STATUS status, std::vector<std::string> addrs,
                    std::string error = std::string(), bool usable = false)
      : family_(family),
        status_(status),
        addrs_(std::move(addrs)),
        error_(std::move(error)),
        usable_(usable)
  {
  }

  void addSocket(sock_t fd, int events)
  {
    socks_.push_back(AsyncResolverSocketEntry{fd, events});
  }

  void resolve(const std::string& name) CXX11_OVERRIDE { hostname_ = name; }

  const std::vector<std::string>& getResolvedAddresses() const CXX11_OVERRIDE
  {
    return addrs_;
  }

  const std::string& getError() const CXX11_OVERRIDE { return error_; }

  STATUS getStatus() const CXX11_OVERRIDE { return status_; }

  bool usable() const CXX11_OVERRIDE { return usable_; }

  int getFamily() const CXX11_OVERRIDE { return family_; }

  const std::vector<AsyncResolverSocketEntry>& getsock() const CXX11_OVERRIDE
  {
    return socks_;
  }

  void process(sock_t readfd, sock_t writefd) CXX11_OVERRIDE
  {
    ++processCount;
    lastReadfd = readfd;
    lastWritefd = writefd;
  }

  void processTimeout() CXX11_OVERRIDE { ++processTimeoutCount; }

  const std::string& getHostname() const CXX11_OVERRIDE { return hostname_; }
};
} // namespace

void PlainBootstrapResolverTest::testResolveSucceedsWhenAnyChildSucceeds()
{
  std::vector<std::shared_ptr<AsyncResolver>> children;
  children.push_back(std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_ERROR, std::vector<std::string>(),
      "udp failed"));

  std::vector<std::string> addrs;
  addrs.push_back("192.0.2.1");
  children.push_back(std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_SUCCESS, addrs));

  PlainBootstrapResolver resolver(AF_INET, std::move(children));
  resolver.resolve("dns.example.org");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_SUCCESS, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL((size_t)1, resolver.getResolvedAddresses().size());
  CPPUNIT_ASSERT_EQUAL(std::string("192.0.2.1"),
                       resolver.getResolvedAddresses()[0]);
  CPPUNIT_ASSERT_EQUAL(std::string("dns.example.org"), resolver.getHostname());
}

void PlainBootstrapResolverTest::testResolveFailsWhenAllChildrenFail()
{
  std::vector<std::shared_ptr<AsyncResolver>> children;
  children.push_back(std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_ERROR, std::vector<std::string>(),
      "udp failed"));
  children.push_back(std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_ERROR, std::vector<std::string>(),
      "tcp failed"));

  PlainBootstrapResolver resolver(AF_INET, std::move(children));
  resolver.resolve("dns.example.org");

  CPPUNIT_ASSERT_EQUAL(AsyncResolver::STATUS_ERROR, resolver.getStatus());
  CPPUNIT_ASSERT_EQUAL(std::string("tcp failed"), resolver.getError());
}

void PlainBootstrapResolverTest::testProcessOnlyDispatchesMatchingSocket()
{
  auto first = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>(),
      std::string(), true);
  first->addSocket(10, 1);
  auto second = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>(),
      std::string(), true);
  second->addSocket(20, 2);

  std::vector<std::shared_ptr<AsyncResolver>> children;
  children.push_back(first);
  children.push_back(second);

  PlainBootstrapResolver resolver(AF_INET, std::move(children));
  resolver.resolve("dns.example.org");
  resolver.process(10, AsyncResolver::badSocket());

  CPPUNIT_ASSERT_EQUAL((size_t)1, first->processCount);
  CPPUNIT_ASSERT_EQUAL((size_t)0, second->processCount);
  CPPUNIT_ASSERT_EQUAL((sock_t)10, first->lastReadfd);
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), first->lastWritefd);
}

void PlainBootstrapResolverTest::testProcessEmptySocketDispatchesAllChildren()
{
  auto first = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>());
  auto second = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>());

  std::vector<std::shared_ptr<AsyncResolver>> children;
  children.push_back(first);
  children.push_back(second);

  PlainBootstrapResolver resolver(AF_INET, std::move(children));
  resolver.resolve("dns.example.org");
  resolver.process(AsyncResolver::badSocket(), AsyncResolver::badSocket());

  CPPUNIT_ASSERT_EQUAL((size_t)1, first->processCount);
  CPPUNIT_ASSERT_EQUAL((size_t)1, second->processCount);
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), first->lastReadfd);
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), first->lastWritefd);
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), second->lastReadfd);
  CPPUNIT_ASSERT_EQUAL(AsyncResolver::badSocket(), second->lastWritefd);
}

void PlainBootstrapResolverTest::testProcessTimeoutDispatchesAllChildren()
{
  auto first = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>());
  auto second = std::make_shared<MockAsyncResolver>(
      AF_INET, AsyncResolver::STATUS_QUERYING, std::vector<std::string>());

  std::vector<std::shared_ptr<AsyncResolver>> children;
  children.push_back(first);
  children.push_back(second);

  PlainBootstrapResolver resolver(AF_INET, std::move(children));
  resolver.resolve("dns.example.org");
  resolver.processTimeout();

  CPPUNIT_ASSERT_EQUAL((size_t)1, first->processTimeoutCount);
  CPPUNIT_ASSERT_EQUAL((size_t)1, second->processTimeoutCount);
}

} // namespace aria2
