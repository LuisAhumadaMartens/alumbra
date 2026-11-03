#include "umbra/ads/media_file_selector.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {
namespace {

class FakeCapabilities final : public penumbra::CapabilityProvider {
 public:
  absl::StatusOr<penumbra::DecodingCapability> QueryDecoding(
      const penumbra::CapabilityRequest& request) const override {
    requests.push_back(request);
    const auto found =
        std::ranges::find_if(support, [&request](const auto& entry) {
          return entry.first == request.stream.codec_string;
        });
    if (found == support.end()) {
      return penumbra::DecodingCapability{.supported = true, .smooth = true};
    }
    return found->second;
  }

  absl::StatusOr<bool> CanChangeType(
      const penumbra::StreamConfig& from,
      const penumbra::StreamConfig& to) const override {
    static_cast<void>(from);
    static_cast<void>(to);
    return false;
  }

  mutable std::vector<penumbra::CapabilityRequest> requests;
  std::vector<std::pair<std::string, penumbra::DecodingCapability>> support;
};

VastMediaFile File(std::string url, std::string delivery, std::string type,
                   std::string codec, std::uint64_t bitrate,
                   std::uint32_t width, std::uint32_t height) {
  return {.url = std::move(url),
          .delivery = std::move(delivery),
          .type = std::move(type),
          .codec = std::move(codec),
          .bitrate_bps = bitrate,
          .width = width,
          .height = height,
          .api_framework = {},
          .scalable = false,
          .maintain_aspect_ratio = false,
          .mezzanine = false};
}

TEST(MediaFileSelectorTest, ChoosesClosestSupportedFileWithinBandwidth) {
  VastCreative creative;
  creative.media_files = {
      File("https://example.test/low.mp4", "progressive", "video/mp4",
           "avc1.42E01E", 400000, 640, 360),
      File("https://example.test/match.mp4", "progressive", "video/mp4",
           "avc1.4D401F", 900000, 1280, 720),
      File("https://example.test/high.mp4", "progressive", "video/mp4",
           "avc1.640028", 2400000, 1920, 1080),
  };
  FakeCapabilities capabilities;
  const MediaFileSelection selected =
      SelectMediaFile(creative,
                      {.preferred_delivery = PreferredDelivery::kAny,
                       .estimated_bandwidth_bps = 1000000,
                       .viewport_width = 1280,
                       .viewport_height = 720},
                      capabilities);
  ASSERT_TRUE(selected.index.has_value());
  EXPECT_EQ(*selected.index, 1U);
  EXPECT_EQ(selected.error_code, 0U);
  ASSERT_EQ(capabilities.requests.size(), 2U);
  EXPECT_EQ(capabilities.requests[1].bitrate, 900000);
}

TEST(MediaFileSelectorTest, RejectsVpaidAndUnsupportedOrUnmatchedFiles) {
  VastCreative creative;
  creative.media_files = {
      File("https://example.test/vpaid.mp4", "progressive", "video/mp4",
           "avc1.42E01E", 300000, 640, 360),
      File("https://example.test/other.mp4", "progressive", "video/mp4",
           "vp09.00.10.08", 300000, 640, 360),
  };
  creative.media_files[0].api_framework = "vPaId";
  FakeCapabilities capabilities;
  capabilities.support.push_back(
      {"vp09.00.10.08", {.supported = false, .smooth = false}});
  const MediaFileSelection selected =
      SelectMediaFile(creative,
                      {.preferred_delivery = PreferredDelivery::kAny,
                       .estimated_bandwidth_bps = 1000000,
                       .viewport_width = 640,
                       .viewport_height = 360},
                      capabilities);
  EXPECT_FALSE(selected.index.has_value());
  EXPECT_EQ(selected.error_code, 403U);
  ASSERT_EQ(capabilities.requests.size(), 1U);
}

TEST(MediaFileSelectorTest, AppliesDeliveryAndMaximumBitrateSettings) {
  VastCreative creative;
  creative.media_files = {
      File("https://example.test/progressive.mp4", "progressive", "video/mp4",
           "avc1.42E01E", 300000, 640, 360),
      File("https://example.test/stream.m3u8", "streaming",
           "application/vnd.apple.mpegurl", "avc1.42E01E", 500000, 640, 360),
  };
  const FakeCapabilities capabilities;
  const MediaFileSelection selected =
      SelectMediaFile(creative,
                      {.preferred_delivery = PreferredDelivery::kStreaming,
                       .max_bitrate_bps = 450000,
                       .estimated_bandwidth_bps = 1000000,
                       .viewport_width = 640,
                       .viewport_height = 360},
                      capabilities);
  ASSERT_TRUE(selected.index.has_value());
  EXPECT_EQ(*selected.index, 0U);
  EXPECT_EQ(selected.error_code, 0U);
  ASSERT_EQ(capabilities.requests.size(), 1U);
}

TEST(MediaFileSelectorTest, ParsesDeliveryPreferenceStrictly) {
  EXPECT_EQ(ParsePreferredDelivery("any"), PreferredDelivery::kAny);
  EXPECT_EQ(ParsePreferredDelivery("progressive"),
            PreferredDelivery::kProgressive);
  EXPECT_EQ(ParsePreferredDelivery("streaming"), PreferredDelivery::kStreaming);
  EXPECT_FALSE(ParsePreferredDelivery("invalid").has_value());
}

TEST(MediaFileSelectorTest, ConformanceFixtureSelectsExpectedMediaFile) {
  std::ifstream input(std::string(ALUMBRA_VAST_FIXTURE_DIR) +
                      "/conformance/vast_inline.xml");
  const std::string xml{std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>()};
  auto document = ParseVast(xml);
  ASSERT_TRUE(document.ok()) << document.status();
  ASSERT_EQ(document->ads.size(), 1U);
  ASSERT_EQ(document->ads.front().creatives.size(), 1U);
  const FakeCapabilities capabilities;
  const MediaFileSelection selection =
      SelectMediaFile(document->ads.front().creatives.front(),
                      {.preferred_delivery = PreferredDelivery::kProgressive,
                       .estimated_bandwidth_bps = 1000000,
                       .viewport_width = 640,
                       .viewport_height = 360},
                      capabilities);
  ASSERT_TRUE(selection.index.has_value());
  EXPECT_EQ(*selection.index, 0U);
  EXPECT_EQ(selection.error_code, 0U);
}

}
}
