#include "umbra/ads/vast_tracking.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "umbra/ads/provider.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {
namespace {

std::string ReadFixture(const std::string& name) {
  std::ifstream input(std::string(ALUMBRA_VAST_FIXTURE_DIR) + "/" + name);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

TEST(VastTrackingTest, MatchesGoldenBeaconSequenceAndOnceOnlyRules) {
  const std::string xml = ReadFixture("tracking.xml");
  ASSERT_FALSE(xml.empty());
  auto parsed = ParseVast(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->ads.size(), 1U);
  ASSERT_EQ(parsed->ads[0].creatives.size(), 1U);
  const VastAd& ad = parsed->ads[0];
  const VastCreative& creative = ad.creatives[0];
  std::vector<std::string> beacons;
  VastTrackingSession session(
      [&beacons](std::string url) { beacons.push_back(std::move(url)); });
  TrackingMacroContext context;
  context.cache_busting = "12345678";
  context.timestamp = "2026-11-03T00:00:00.000Z";
  context.app_bundle = "org.example.player";
  context.ad_playhead = penumbra::TimeDelta::Zero();
  context.media_playhead = penumbra::TimeDelta::FromSeconds(12);

  AdProviderEvent event;
  event.type = AdProviderEvent::Type::kAdStart;
  event.duration = penumbra::TimeDelta::FromSeconds(20);
  session.HandleEvent(event, ad, creative, context);
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdProgress;
  event.current_time = penumbra::TimeDelta::FromSeconds(5);
  session.HandleEvent(event, ad, creative, context);
  event.current_time = penumbra::TimeDelta::FromSeconds(1);
  session.HandleEvent(event, ad, creative, context);
  event.current_time = penumbra::TimeDelta::FromSeconds(5);
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdQuartile;
  event.quartile = 1;
  session.HandleEvent(event, ad, creative, context);
  session.HandleEvent(event, ad, creative, context);
  event.quartile = 2;
  session.HandleEvent(event, ad, creative, context);
  event.quartile = 3;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdClick;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdMute;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdUnmute;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdPause;
  session.HandleEvent(event, ad, creative, context);
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdResume;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdComplete;
  session.HandleEvent(event, ad, creative, context);
  event.type = AdProviderEvent::Type::kAdSkip;
  session.HandleEvent(event, ad, creative, context);
  session.ReportError(ad, "403", context);

  std::string actual;
  for (const std::string& beacon : beacons) {
    actual += beacon + "\n";
  }
  EXPECT_EQ(actual, ReadFixture("tracking.golden.txt"));
}

TEST(VastTrackingTest, EncodesMacroValuesAndWithholdsMissingIdentifiers) {
  TrackingMacroContext context;
  context.timestamp = "2026-11-03T00:00:00.000Z";
  context.gdpr_consent = "abc+/=";
  context.limit_ad_tracking = "1";
  context.media_playhead = penumbra::TimeDelta::Unknown();
  const std::string expanded = ExpandVastMacros(
      "?ts=[TIMESTAMP]&consent=[GDPRCONSENT]&play=[MEDIAPLAYHEAD]"
      "&content=[CONTENTPLAYHEAD]&ifa=[IFA]&app=[APPBUNDLE]&lat=["
      "LIMITADTRACKING]",
      context);
  EXPECT_EQ(expanded,
            "?ts=2026-11-03T00%3A00%3A00.000Z&consent=abc%2B%2F%3D&play=-1"
            "&content=-1&ifa=-2&app=-2&lat=1");
}

TEST(VastTrackingTest, SendsErrorToEveryResolvedWrapperLevel) {
  const std::string wrapper =
      "<VAST version=\"4.3\"><Ad id=\"outer\"><Wrapper>"
      "<Error>https://track.test/outer?code=[ERRORCODE]</Error>"
      "<VASTAdTagURI>https://ads.test/inline</VASTAdTagURI>"
      "</Wrapper></Ad></VAST>";
  const std::string inline_xml =
      "<VAST version=\"4.3\"><Ad id=\"inner\"><InLine>"
      "<Error>https://track.test/inner?code=[ERRORCODE]</Error>"
      "</InLine></Ad></VAST>";
  auto resolved = ResolveVastWrappers(
      wrapper,
      [&inline_xml](std::string_view, std::uint32_t)
          -> absl::StatusOr<std::string> { return inline_xml; });
  ASSERT_TRUE(resolved.ok()) << resolved.status();
  ASSERT_EQ(resolved->ads.size(), 1U);
  std::vector<std::string> beacons;
  VastTrackingSession session(
      [&beacons](std::string url) { beacons.push_back(std::move(url)); });
  session.ReportError(resolved->ads[0], "301", {});
  EXPECT_EQ(beacons,
            (std::vector<std::string>{"https://track.test/outer?code=301",
                                      "https://track.test/inner?code=301"}));
}

}
}
