#include "umbra/ads/vast_parser.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace umbra::ads {
namespace {

constexpr std::string_view kInline =
    "<VAST version=\"4.3\"><Ad id=\"ad-1\" sequence=\"1\" adType=\"video\">"
    "<InLine><Impression>https://example.test/impression</Impression>"
    "<Error>https://example.test/error</Error><Creatives><Creative id=\"c1\" "
    "adId=\"universal-1\"><UniversalAdId idRegistry=\"ad-id.org\">"
    "universal-1</UniversalAdId><Linear skipoffset=\"00:00:05\"><MediaFiles>"
    "<MediaFile delivery=\"progressive\" type=\"video/mp4\" bitrate=\"900\" "
    "width=\"640\" height=\"360\">https://example.test/ad.mp4</MediaFile>"
    "<Mezzanine delivery=\"progressive\" type=\"video/mp4\" width=\"1920\" "
    "height=\"1080\">https://example.test/master.mp4</Mezzanine></MediaFiles>"
    "<TrackingEvents><Tracking event=\"start\">https://example.test/start"
    "</Tracking></TrackingEvents><VideoClicks><ClickThrough>https://"
    "example.test"
    "</ClickThrough><ClickTracking>https://example.test/click</ClickTracking>"
    "</VideoClicks></Linear></Creative></"
    "Creatives><AdVerifications><Verification "
    "vendor=\"vendor\"><JavaScriptResource>https://example.test/verify.js"
    "</JavaScriptResource><Tracking event=\"verificationNotExecuted\">"
    "https://example.test/not-executed</Tracking></Verification></"
    "AdVerifications>"
    "<Extensions><Extension type=\"sample\"><Value>raw</Value></Extension>"
    "</Extensions></InLine></Ad></VAST>";

constexpr std::string_view kWrapper =
    "<VAST version=\"4.2\"><Ad id=\"wrapper\"><Wrapper "
    "followAdditionalWrappers=\"true\" allowMultipleAds=\"false\" "
    "fallbackOnNoAd=\"true\"><AdSystem>test</AdSystem><VASTAdTagURI>"
    "https://example.test/next</VASTAdTagURI><Impression>"
    "https://example.test/wrapper-impression</Impression><Error>"
    "https://example.test/wrapper-error</Error><Creatives><Creative><Linear>"
    "<TrackingEvents><Tracking event=\"start\">"
    "https://example.test/wrapper-start</Tracking></TrackingEvents></Linear>"
    "</Creative></Creatives></Wrapper></Ad></VAST>";

std::string Wrapped(std::string_view uri) {
  return R"(<VAST version="4.3"><Ad id="w"><Wrapper><VASTAdTagURI>)" +
         std::string(uri) + "</VASTAdTagURI></Wrapper></Ad></VAST>";
}

std::string ReadFixture(std::string_view name) {
  std::ifstream input(std::string(ALUMBRA_VAST_FIXTURE_DIR) + "/" +
                      std::string(name));
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string ToGoldenJson(const VastDocument& document) {
  std::ostringstream output;
  output << R"({"version":")" << document.version << R"(","ads":[)";
  for (std::size_t ad_index = 0; ad_index < document.ads.size(); ++ad_index) {
    if (ad_index != 0) {
      output << ',';
    }
    const VastAd& ad = document.ads[ad_index];
    output << R"({"id":")" << ad.id << R"(","sequence":)"
           << ad.sequence.value_or(0) << R"(,"ad_type":")" << ad.ad_type
           << R"(","media_files":[)";
    bool first = true;
    for (const VastCreative& creative : ad.creatives) {
      for (const VastMediaFile& media : creative.media_files) {
        if (!first) {
          output << ',';
        }
        first = false;
        output << R"({"url":")" << media.url << R"(","type":")" << media.type
               << R"(","bitrate_bps":)" << media.bitrate_bps.value_or(0)
               << ",\"width\":" << media.width.value_or(0)
               << ",\"height\":" << media.height.value_or(0) << '}';
      }
    }
    output << "]}";
  }
  output << "]}";
  return output.str();
}

TEST(VastParserTest, MatchesInlineGoldenJson) {
  auto result = ParseVast(ReadFixture("inline.xml"));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(ToGoldenJson(*result) + "\n", ReadFixture("inline.golden.json"));
}

TEST(VastParserTest, ParsesInlineMediaTrackingVerificationAndExtensions) {
  auto result = ParseVast(kInline);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->version, "4.3");
  ASSERT_EQ(result->ads.size(), 1);
  const VastAd& ad = result->ads.front();
  EXPECT_EQ(ad.id, "ad-1");
  EXPECT_EQ(ad.sequence, 1);
  EXPECT_EQ(ad.ad_type, "video");
  ASSERT_EQ(ad.creatives.size(), 1);
  const VastCreative& creative = ad.creatives.front();
  EXPECT_EQ(creative.skip_offset, "00:00:05");
  ASSERT_EQ(creative.media_files.size(), 2);
  EXPECT_EQ(creative.media_files[0].bitrate_bps, 900);
  EXPECT_FALSE(creative.media_files[0].mezzanine);
  EXPECT_TRUE(creative.media_files[1].mezzanine);
  ASSERT_EQ(creative.tracking_events.size(), 1);
  EXPECT_EQ(creative.tracking_events[0].event, "start");
  ASSERT_EQ(ad.universal_ad_ids.size(), 1);
  EXPECT_EQ(ad.universal_ad_ids[0].id, "universal-1");
  ASSERT_EQ(ad.verifications.size(), 1);
  EXPECT_EQ(ad.verifications[0].not_executed_reason, 2);
  ASSERT_EQ(ad.extensions_xml.size(), 1);
  EXPECT_EQ(ad.extensions_xml[0],
            "<Extension type=\"sample\"><Value>raw</Value></Extension>");
}

TEST(VastParserTest, AcceptsVastTwoAndThreeAndRejectsUnsupportedVersion) {
  for (std::string_view const version : {"2.0", "3.0", "4.0", "4.3", "4.4"}) {
    const std::string xml = "<VAST version=\"" + std::string(version) + "\"/>";
    EXPECT_TRUE(ParseVast(xml).ok());
  }
  auto result = ParseVast("<VAST version=\"1.0\"/>");
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("VAST-102"), std::string::npos);
  for (std::string_view const version : {"2.3", "3.1", "4.x"}) {
    const std::string unsupported =
        "<VAST version=\"" + std::string(version) + "\"/>";
    EXPECT_FALSE(ParseVast(unsupported).ok());
  }
}

TEST(VastParserTest, RejectsDtdEntitiesAndOversizedDocuments) {
  const std::string xxe_payload =
      "<!DOCTYPE VAST [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>"
      "<VAST version=\"4.3\"><Ad>&x;</Ad></VAST>";
  auto xxe = ParseVast(xxe_payload);
  ASSERT_FALSE(xxe.ok());
  EXPECT_NE(xxe.status().message().find("VAST-100"), std::string::npos);
  std::size_t fetches = 0;
  auto xxe_resolution = ResolveVastWrappers(
      xxe_payload,
      [&fetches](std::string_view,
                 std::uint32_t) -> absl::StatusOr<std::string> {
        ++fetches;
        return std::string();
      });
  EXPECT_FALSE(xxe_resolution.ok());
  EXPECT_EQ(fetches, 0);
  VastParserOptions options;
  options.max_response_bytes = 10;
  auto oversized = ParseVast(kInline, options);
  ASSERT_FALSE(oversized.ok());
  EXPECT_NE(oversized.status().message().find("VAST-100"), std::string::npos);
}

TEST(VastParserTest, DecodesPredefinedAndNumericXmlEntities) {
  auto result = ParseVast(
      "<VAST version=\"4.3\"><Ad id=\"a\"><InLine><Impression>"
      "https://example.test/i?a=1&amp;b=&#50;</Impression></InLine></Ad>"
      "</VAST>");
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->ads.size(), 1);
  EXPECT_EQ(result->ads[0].impressions[0], "https://example.test/i?a=1&b=2");
}

TEST(VastParserTest, ResolvesWrapperChainAndMergesTrackingData) {
  std::vector<std::string> requested;
  auto result = ResolveVastWrappers(
      kWrapper,
      [&requested](std::string_view uri,
                   std::uint32_t timeout_ms) -> absl::StatusOr<std::string> {
        EXPECT_EQ(timeout_ms, 5000);
        requested.emplace_back(uri);
        return std::string(kInline);
      });
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(requested.size(), 1);
  ASSERT_EQ(result->ads.size(), 1);
  EXPECT_EQ(result->ads[0].impressions.size(), 2);
  EXPECT_EQ(result->ads[0].error_urls.size(), 2);
  ASSERT_EQ(result->ads[0].creatives[0].tracking_events.size(), 2);
  EXPECT_EQ(result->ads[0].creatives[0].tracking_events[0].url,
            "https://example.test/wrapper-start");
}

TEST(VastParserTest, AllowsDepthLimitAndRejectsOneMoreWrapper) {
  const std::string wrapped = Wrapped("https://example.test/next");
  for (std::size_t allowed = 1; allowed <= 5; ++allowed) {
    std::size_t requests = 0;
    VastResolverOptions options;
    options.max_wrapper_depth = allowed;
    auto result = ResolveVastWrappers(
        wrapped,
        [&requests, allowed](std::string_view,
                             std::uint32_t) -> absl::StatusOr<std::string> {
          ++requests;
          if (requests == allowed) {
            return std::string(kInline);
          }
          return Wrapped("https://example.test/next-" +
                         std::to_string(requests));
        },
        options);
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_EQ(requests, allowed);
  }
  std::size_t requests = 0;
  auto result = ResolveVastWrappers(
      wrapped,
      [&requests](std::string_view,
                  std::uint32_t) -> absl::StatusOr<std::string> {
        ++requests;
        return Wrapped("https://example.test/next-" + std::to_string(requests));
      });
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("VAST-302"), std::string::npos);
  EXPECT_EQ(requests, 5);
}

TEST(VastParserTest, DetectsLoopWithoutRepeatingNetworkRequest) {
  std::size_t requests = 0;
  auto result = ResolveVastWrappers(
      Wrapped("https://example.test/repeat"),
      [&requests](std::string_view,
                  std::uint32_t) -> absl::StatusOr<std::string> {
        ++requests;
        return Wrapped("https://example.test/repeat");
      });
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(requests, 1);
}

TEST(VastParserTest, MapsFetchFailureToWrapperTimeout) {
  auto result = ResolveVastWrappers(
      Wrapped("https://example.test/timeout"),
      [](std::string_view, std::uint32_t) -> absl::StatusOr<std::string> {
        return absl::DeadlineExceededError("request timed out");
      });
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("VAST-301"), std::string::npos);
}

}
}
