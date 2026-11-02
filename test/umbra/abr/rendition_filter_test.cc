#include "umbra/abr/rendition_filter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::abr {
namespace {

manifest::Track MakeTrack(std::string id, std::int32_t height,
                          std::int64_t bitrate, std::uint8_t profile = 100) {
  manifest::Track track;
  track.id = manifest::TrackId(std::move(id));
  track.bandwidth_bps = bitrate;
  track.codecs.emplace_back(codec::AvcCodec{
      .profile_idc = profile, .constraint_flags = 0, .level_idc = 31});
  track.format =
      manifest::VideoTrackFormat{.width = height * 16 / 9,
                                 .height = height,
                                 .frame_rate = penumbra::Rational::Of(30, 1),
                                 .bit_depth = 8,
                                 .range = manifest::VideoRange::kSdr};
  return track;
}

RenditionDescriptor Describe(const manifest::Track& track) {
  return RenditionDescriptor{
      .id = track.id.Value(),
      .height = std::get<manifest::VideoTrackFormat>(track.format).height,
      .bitrate_bps = track.bandwidth_bps};
}

class FakeCapabilityProvider : public penumbra::CapabilityProvider {
 public:
  absl::StatusOr<penumbra::DecodingCapability> QueryDecoding(
      const penumbra::CapabilityRequest& request) const override {
    requests.push_back(request);
    const bool supported = request.stream.codec_string != "avc1.64001f";
    return penumbra::DecodingCapability{
        .supported = supported,
        .smooth = supported,
        .power_efficient = false,
        .preferred_output_mode = penumbra::OutputMode::kTexture,
        .accepts_fragments = false};
  }

  absl::StatusOr<bool> CanChangeType(
      const penumbra::StreamConfig& from,
      const penumbra::StreamConfig& to) const override {
    static_cast<void>(from);
    static_cast<void>(to);
    return false;
  }

  mutable std::vector<penumbra::CapabilityRequest> requests;
};

TEST(RenditionFilterTest, FiltersUnsupportedAndNotSmoothRenditions) {
  const auto low = MakeTrack("low", 360, 500'000, 77);
  const auto mid = MakeTrack("mid", 720, 1'500'000);
  const auto high = MakeTrack("high", 1080, 4'000'000);
  const std::vector<RenditionDescriptor> renditions{
      Describe(low), Describe(mid), Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "mid", .supported = true, .smooth = false},
      {.id = "high", .supported = false, .smooth = false}};

  const auto result =
      FilterRenditions(renditions, capabilities, FilterPolicy{});

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->eligible_indices.size(), 1);
  EXPECT_EQ(result->eligible_indices.front(), 0);
  ASSERT_EQ(result->excluded.size(), 2);
  EXPECT_EQ(result->excluded[0].id, "mid");
  EXPECT_EQ(result->excluded[0].reason, FilterReason::kNotSmooth);
  EXPECT_EQ(result->excluded[1].id, "high");
  EXPECT_EQ(result->excluded[1].reason, FilterReason::kUnsupported);
}

TEST(RenditionFilterTest, EmptyResultPreservesReasonsForSourceFailure) {
  const auto track = MakeTrack("unsupported", 1080, 4'000'000);
  const std::vector<RenditionDescriptor> renditions{Describe(track)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "unsupported", .supported = false, .smooth = false}};

  const auto result =
      FilterRenditions(renditions, capabilities, FilterPolicy{});

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->eligible_indices.empty());
  ASSERT_EQ(result->excluded.size(), 1);
  EXPECT_EQ(result->excluded.front().reason, FilterReason::kUnsupported);
}

TEST(RenditionFilterTest, ViewportCapTracksResizeInBothDirections) {
  const auto low = MakeTrack("low", 360, 500'000);
  const auto medium = MakeTrack("medium", 720, 1'500'000);
  const auto high = MakeTrack("high", 1080, 4'000'000);
  const std::vector<RenditionDescriptor> renditions{
      Describe(low), Describe(medium), Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "medium", .supported = true, .smooth = true},
      {.id = "high", .supported = true, .smooth = true}};

  FilterPolicy small_viewport;
  small_viewport.viewport_height_pixels = 360;
  auto small_result =
      FilterRenditions(renditions, capabilities, small_viewport);
  ASSERT_TRUE(small_result.ok()) << small_result.status();
  ASSERT_EQ(small_result->eligible_indices.size(), 1);
  EXPECT_EQ(small_result->eligible_indices.front(), 0);

  FilterPolicy large_viewport;
  large_viewport.viewport_height_pixels = 1080;
  auto large_result =
      FilterRenditions(renditions, capabilities, large_viewport);
  ASSERT_TRUE(large_result.ok()) << large_result.status();
  ASSERT_EQ(large_result->eligible_indices.size(), 3);
}

TEST(RenditionFilterTest, HeightAndBitrateCapsHaveDistinctReasons) {
  const auto low = MakeTrack("low", 360, 500'000);
  const auto high_bitrate = MakeTrack("high_bitrate", 720, 4'000'000);
  const auto high = MakeTrack("high", 1080, 1'500'000);
  const std::vector<RenditionDescriptor> renditions{
      Describe(low), Describe(high_bitrate), Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "high_bitrate", .supported = true, .smooth = true},
      {.id = "high", .supported = true, .smooth = true}};
  FilterPolicy policy;
  policy.max_height = 720;
  policy.max_bitrate_bps = 1'500'000;

  const auto result = FilterRenditions(renditions, capabilities, policy);

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->eligible_indices.size(), 1);
  EXPECT_EQ(result->eligible_indices.front(), 0);
  ASSERT_EQ(result->excluded.size(), 2);
  EXPECT_EQ(result->excluded[0].reason, FilterReason::kMaximumBitrate);
  EXPECT_EQ(result->excluded[1].reason, FilterReason::kMaximumHeight);
}

TEST(RenditionFilterTest, ManualSelectionKeepsOnlySelectedTrack) {
  const auto low = MakeTrack("low", 360, 500'000);
  const auto medium = MakeTrack("medium", 720, 1'500'000);
  const auto high = MakeTrack("high", 1080, 4'000'000);
  const std::vector<RenditionDescriptor> renditions{
      Describe(low), Describe(medium), Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "medium", .supported = true, .smooth = true},
      {.id = "high", .supported = true, .smooth = true}};
  FilterPolicy policy;
  policy.manual_selection = "medium";

  const auto result = FilterRenditions(renditions, capabilities, policy);

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->eligible_indices.size(), 1);
  EXPECT_EQ(result->eligible_indices.front(), 1);
  ASSERT_EQ(result->excluded.size(), 2);
  EXPECT_EQ(result->excluded[0].reason, FilterReason::kManualSelection);
  EXPECT_EQ(result->excluded[1].reason, FilterReason::kManualSelection);
}

TEST(RenditionFilterTest, AppliesDataSaverBitrateCap) {
  const auto low = MakeTrack("low", 360, 500'000);
  const auto high = MakeTrack("high", 720, 1'500'000);
  const std::vector<RenditionDescriptor> renditions{Describe(low),
                                                    Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "high", .supported = true, .smooth = true}};
  FilterPolicy policy;
  policy.data_saver_max_bitrate_bps = 1'000'000;

  const auto result = FilterRenditions(renditions, capabilities, policy);

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->eligible_indices.size(), 1);
  ASSERT_EQ(result->excluded.size(), 1);
  EXPECT_EQ(result->excluded.front().reason, FilterReason::kDataSaver);
}

TEST(RenditionFilterTest, AppliesCmsdAndDecodeDropCaps) {
  const auto low = MakeTrack("low", 360, 500'000);
  const auto high_bitrate = MakeTrack("high_bitrate", 720, 1'500'000);
  const auto high = MakeTrack("high", 1080, 1'000'000);
  const std::vector<RenditionDescriptor> renditions{
      Describe(low), Describe(high_bitrate), Describe(high)};
  const std::vector<RenditionCapability> capabilities{
      {.id = "low", .supported = true, .smooth = true},
      {.id = "high_bitrate", .supported = true, .smooth = true},
      {.id = "high", .supported = true, .smooth = true}};
  FilterPolicy policy;
  policy.cmsd_max_bitrate_bps = 1'000'000;
  policy.decode_drop_max_height = 720;

  const auto result = FilterRenditions(renditions, capabilities, policy);

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->eligible_indices.size(), 1);
  EXPECT_EQ(result->eligible_indices.front(), 0);
  ASSERT_EQ(result->excluded.size(), 2);
  EXPECT_EQ(result->excluded[0].reason, FilterReason::kCmsd);
  EXPECT_EQ(result->excluded[1].reason, FilterReason::kDecodeDropCap);
}

TEST(RenditionFilterTest, CapabilityQueryCarriesCodecResolutionAndFrameRate) {
  auto track = MakeTrack("video", 1080, 4'000'000);
  track.codecs.insert(track.codecs.begin(), codec::Mp4aCodec{});
  std::get<manifest::VideoTrackFormat>(track.format).frame_rate =
      penumbra::Rational::Of(120, 1);
  const std::vector<const manifest::Track*> tracks{&track};
  FakeCapabilityProvider provider;

  const auto result = QueryRenditionCapabilities(tracks, provider);

  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->size(), 1);
  EXPECT_FALSE(result->front().supported);
  ASSERT_EQ(provider.requests.size(), 1);
  EXPECT_EQ(provider.requests.front().stream.codec_string, "avc1.64001f");
  const auto& video =
      std::get<penumbra::VideoInfo>(provider.requests.front().stream.format);
  EXPECT_EQ(video.coded_height, 1080);
  EXPECT_EQ(video.frame_rate, penumbra::Rational::Of(120, 1));
  EXPECT_EQ(provider.requests.front().bitrate, 4'000'000);
}

TEST(RenditionFilterTest, NextLoadCapsRemainFixedAndViewportSettingIsLive) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator, settings::kAbrMaxHeight,
                       std::int64_t{720})
                  .ok());
  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator,
                       settings::kAbrMaxBitrateBps, std::int64_t{2'000'000})
                  .ok());
  ASSERT_TRUE(
      store
          .Set(settings::Layer::kIntegrator, settings::kAbrCapToViewport, false)
          .ok());

  const settings::Snapshot load_settings = store.TakeSnapshot();
  const FilterPolicy policy = ReadFilterPolicy(load_settings, false, 1080);

  EXPECT_EQ(policy.max_height, 720);
  EXPECT_EQ(policy.max_bitrate_bps, 2'000'000);
  EXPECT_FALSE(policy.cap_to_viewport);
  EXPECT_EQ(policy.viewport_height_pixels, 0);

  ASSERT_TRUE(store
                  .Set(settings::Layer::kUser, settings::kAbrMaxHeight,
                       std::int64_t{360})
                  .ok());
  ASSERT_TRUE(
      store.Set(settings::Layer::kUser, settings::kAbrCapToViewport, true)
          .ok());
  const FilterPolicy same_load = ReadFilterPolicy(
      load_settings, store.Get(settings::kAbrCapToViewport), 360);
  EXPECT_EQ(same_load.max_height, 720);
  EXPECT_TRUE(same_load.cap_to_viewport);
  EXPECT_EQ(same_load.viewport_height_pixels, 360);

  const FilterPolicy next_load = ReadFilterPolicy(
      store.TakeSnapshot(), store.Get(settings::kAbrCapToViewport), 360);
  EXPECT_EQ(next_load.max_height, 360);
  EXPECT_TRUE(next_load.cap_to_viewport);
}

TEST(RenditionFilterTest, RejectsNegativeCaps) {
  FilterPolicy policy;
  policy.max_height = -1;
  const std::vector<RenditionDescriptor> renditions;
  const std::vector<RenditionCapability> capabilities;

  const auto result = FilterRenditions(renditions, capabilities, policy);

  EXPECT_FALSE(result.ok());
}

}
}
