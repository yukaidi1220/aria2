#include "common.h"

#ifdef HAVE_LIBNGHTTP2

#  include "Http2Session.h"

#  include <string>

#  include <cppunit/extensions/HelperMacros.h>

namespace aria2 {

class Http2SessionTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(Http2SessionTest);
  CPPUNIT_TEST(testSubmitRequestHeadersProducesClientBytes);
  CPPUNIT_TEST(testDrainOutboundDataClearsBuffer);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSubmitRequestHeadersProducesClientBytes();
  void testDrainOutboundDataClearsBuffer();
};

CPPUNIT_TEST_SUITE_REGISTRATION(Http2SessionTest);

namespace {
Http2HeaderBlock createHeaders()
{
  Http2HeaderBlock headers;
  headers.emplace_back(":method", "GET");
  headers.emplace_back(":scheme", "https");
  headers.emplace_back(":authority", "example.org");
  headers.emplace_back(":path", "/file");
  headers.emplace_back("user-agent", "aria2");
  return headers;
}

bool containsFrameType(const std::string& data, unsigned char type)
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
} // namespace

void Http2SessionTest::testSubmitRequestHeadersProducesClientBytes()
{
  Http2Session session;
  auto streamId = session.submitRequestHeaders(createHeaders());
  auto data = session.drainOutboundData();

  CPPUNIT_ASSERT(streamId > 0);
  CPPUNIT_ASSERT_EQUAL((int32_t)1, streamId % 2);
  CPPUNIT_ASSERT(data.size() > 24);
  CPPUNIT_ASSERT_EQUAL(std::string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"),
                       data.substr(0, 24));
  CPPUNIT_ASSERT(containsFrameType(data, 0x04));
}

void Http2SessionTest::testDrainOutboundDataClearsBuffer()
{
  Http2Session session;
  session.submitRequestHeaders(createHeaders());
  CPPUNIT_ASSERT(!session.drainOutboundData().empty());
  CPPUNIT_ASSERT(session.drainOutboundData().empty());
}

} // namespace aria2

#endif // HAVE_LIBNGHTTP2
