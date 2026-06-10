#include "NameResolveCommand.h"

#include <memory>
#include <utility>
#include <vector>

#include <cppunit/extensions/HelperMacros.h>

#include "DHTEntryPointNameResolveCommand.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "SelectEventPoll.h"
#include "UDPTrackerRequest.h"
#include "a2functional.h"
#include "a2netcompat.h"
#include "prefs.h"

namespace aria2 {

class NameResolveCommandTest : public CppUnit::TestFixture {

  CPPUNIT_TEST_SUITE(NameResolveCommandTest);
  CPPUNIT_TEST(testUDPTrackerIgnoresSecureDnsConfigWhenAsyncDnsDisabled);
  CPPUNIT_TEST(testDHTEntryPointIgnoresSecureDnsConfigWhenAsyncDnsDisabled);
  CPPUNIT_TEST_SUITE_END();

public:
  void testUDPTrackerIgnoresSecureDnsConfigWhenAsyncDnsDisabled();
  void testDHTEntryPointIgnoresSecureDnsConfigWhenAsyncDnsDisabled();
};

CPPUNIT_TEST_SUITE_REGISTRATION(NameResolveCommandTest);

namespace {
void configureDisabledAsyncDnsWithBadSecureServer(Option* option)
{
  option->put(PREF_ASYNC_DNS, A2_V_FALSE);
  option->put(PREF_ASYNC_DNS_MODE, V_DOH);
  option->put(PREF_ASYNC_DNS_SERVER, "");
}

} // namespace

void NameResolveCommandTest::
    testUDPTrackerIgnoresSecureDnsConfigWhenAsyncDnsDisabled()
{
  Option option;
  configureDisabledAsyncDnsWithBadSecureServer(&option);
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(&option);
  auto req = std::make_shared<UDPTrackerRequest>();
  req->remoteAddr = "tracker.example.org";

  NameResolveCommand command(1, &e, req);
  CPPUNIT_ASSERT_EQUAL((cuid_t)1, command.getCuid());
}

void NameResolveCommandTest::
    testDHTEntryPointIgnoresSecureDnsConfigWhenAsyncDnsDisabled()
{
  Option option;
  configureDisabledAsyncDnsWithBadSecureServer(&option);
  DownloadEngine e(make_unique<SelectEventPoll>());
  e.setOption(&option);
  std::vector<std::pair<std::string, uint16_t>> entryPoints;
  entryPoints.push_back({"router.example.org", 6881});

  DHTEntryPointNameResolveCommand command(1, &e, AF_INET, entryPoints);
  CPPUNIT_ASSERT_EQUAL((cuid_t)1, command.getCuid());
}

} // namespace aria2
