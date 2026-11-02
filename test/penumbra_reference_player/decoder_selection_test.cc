#include "penumbra/reference_player/decoder_selection.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/reference_player/decoder_blocklist.h"

namespace penumbra {
namespace {

DecoderCandidate Candidate(std::string name, DecoderKind kind,
                           bool secure = false) {
  DecoderCandidate candidate;
  candidate.name = std::move(name);
  candidate.kind = kind;
  candidate.secure = secure;
  candidate.codecs = {CodecId::kH264, CodecId::kAv1};
  return candidate;
}

StreamConfig Video(CodecId codec = CodecId::kH264, int32_t width = 1920,
                   int32_t height = 1080) {
  StreamConfig config;
  config.info.codec = codec;
  VideoInfo video;
  video.coded_width = width;
  video.coded_height = height;
  config.info.format = video;
  return config;
}

std::vector<DecoderCandidate> Pool() {
  return {Candidate("bundled.a", DecoderKind::kBundledSoftware),
          Candidate("platform.a", DecoderKind::kPlatformSoftware),
          Candidate("hardware.a", DecoderKind::kHardware),
          Candidate("hardware.b", DecoderKind::kHardware),
          Candidate("bundled.b", DecoderKind::kBundledSoftware)};
}

std::vector<std::string> Names(const std::vector<DecoderCandidate>& list) {
  std::vector<std::string> names;
  names.reserve(list.size());
  for (const auto& candidate : list) {
    names.push_back(candidate.name);
  }
  return names;
}

SelectionRequest Request(DecoderSettings settings = {}) {
  return {.stream = Video(), .settings = std::move(settings)};
}

constexpr std::array kBlocklist = {
    BlocklistEntry{.id = "hardware-a-old-driver",
                   .decoder = "hardware.a",
                   .codec = "h264",
                   .driver_min = "1.0",
                   .driver_max = "2.5",
                   .reason = "Corrupt output",
                   .evidence = "https://example.org/1",
                   .since = "0.1.0"},
    BlocklistEntry{.id = "platform-a-android",
                   .decoder = "platform.a",
                   .port = "android",
                   .os_min = "12",
                   .os_max = "13",
                   .device_model = "Model X",
                   .reason = "Hangs",
                   .evidence = "https://example.org/2",
                   .since = "0.1.0"},
};

using Names_t = std::vector<std::string>;

TEST(DecoderSelectionTest, OrdersHardwareThenPlatformThenBundled) {
  const auto selected = SelectDecoderCandidates(Pool(), Request());
  EXPECT_EQ(Names(selected), (Names_t{"hardware.a", "hardware.b", "platform.a",
                                      "bundled.a", "bundled.b"}));
}

TEST(DecoderSelectionTest, DropsCandidatesThatDoNotSupportTheStream) {
  auto pool = Pool();
  pool[0].codecs = {CodecId::kAv1};
  pool[1].max_width = 1280;
  pool[2].max_height = 720;
  auto request = Request();
  const auto selected = SelectDecoderCandidates(pool, request);
  EXPECT_EQ(Names(selected), (Names_t{"hardware.b", "bundled.b"}));
  request.stream = Video(CodecId::kAv1, 1280, 720);
  EXPECT_EQ(SelectDecoderCandidates(pool, request).size(), 5U);
  request.stream = Video(CodecId::kHevc);
  EXPECT_TRUE(SelectDecoderCandidates(pool, request).empty());
}

TEST(DecoderSelectionTest, PreferHardwareNeverKeepsSoftwareOnly) {
  const auto selected = SelectDecoderCandidates(
      Pool(), Request({.prefer_hardware = HardwarePreference::kNever}));
  EXPECT_EQ(Names(selected), (Names_t{"platform.a", "bundled.a", "bundled.b"}));
}

TEST(DecoderSelectionTest, PreferHardwareAlwaysKeepsHardwareOnly) {
  const auto selected = SelectDecoderCandidates(
      Pool(), Request({.prefer_hardware = HardwarePreference::kAlways}));
  EXPECT_EQ(Names(selected), (Names_t{"hardware.a", "hardware.b"}));
}

TEST(DecoderSelectionTest, RequiredSecureDecoderSurvivesPreferHardware) {
  const std::vector<DecoderCandidate> pool = {
      Candidate("secure.software", DecoderKind::kPlatformSoftware, true),
      Candidate("secure.hardware", DecoderKind::kHardware, true),
      Candidate("clear.software", DecoderKind::kBundledSoftware)};
  for (const auto preference :
       {HardwarePreference::kNever, HardwarePreference::kAlways}) {
    auto request = Request({.prefer_hardware = preference});
    request.require_secure = true;
    EXPECT_EQ(Names(SelectDecoderCandidates(pool, request)),
              (Names_t{"secure.hardware", "secure.software"}));
  }
}

TEST(DecoderSelectionTest, SecureStreamNeverGetsNonSecureCandidate) {
  const std::vector<DecoderCandidate> pool = {
      Candidate("clear.hardware", DecoderKind::kHardware),
      Candidate("clear.software", DecoderKind::kBundledSoftware)};
  auto request = Request();
  request.require_secure = true;
  EXPECT_TRUE(SelectDecoderCandidates(pool, request).empty());
}

TEST(DecoderSelectionTest, BlocklistRemovesMatchingDecoders) {
  auto request = Request();
  request.blocklist = kBlocklist;
  request.device.driver_version = "2.0";
  EXPECT_EQ(Names(SelectDecoderCandidates(Pool(), request)),
            (Names_t{"hardware.b", "platform.a", "bundled.a", "bundled.b"}));
  request.device.driver_version = "2.6";
  EXPECT_EQ(SelectDecoderCandidates(Pool(), request).size(), 5U);
  request.device = {.port = "android",
                    .os_version = "12.1",
                    .device_model = "Model X",
                    .driver_version = "9"};
  EXPECT_EQ(Names(SelectDecoderCandidates(Pool(), request)),
            (Names_t{"hardware.a", "hardware.b", "bundled.a", "bundled.b"}));
}

TEST(DecoderSelectionTest, IgnoredEntriesAreNotApplied) {
  auto request =
      Request({.blocklist_ignored_entries = {"hardware-a-old-driver"}});
  request.blocklist = kBlocklist;
  request.device.driver_version = "2.0";
  EXPECT_EQ(SelectDecoderCandidates(Pool(), request).size(), 5U);
}

TEST(DecoderSelectionTest, ExtraDecodersExcludeByNameAndCodec) {
  auto request =
      Request({.blocklist_extra_decoders = {"bundled.a", "hardware.b:h264",
                                            "platform.a:av1"}});
  EXPECT_EQ(Names(SelectDecoderCandidates(Pool(), request)),
            (Names_t{"hardware.a", "platform.a", "bundled.b"}));
}

TEST(DecoderSelectionTest, PreferredDecodersReorderTheFilteredList) {
  auto request = Request({.preferred_decoders = {"bundled.b", "platform.a"}});
  EXPECT_EQ(Names(SelectDecoderCandidates(Pool(), request)),
            (Names_t{"bundled.b", "platform.a", "hardware.a", "hardware.b",
                     "bundled.a"}));
}

TEST(DecoderSelectionTest, PreferredDecodersCannotRestoreBlockedOnes) {
  auto request = Request({.preferred_decoders = {"hardware.a", "bundled.a"},
                          .blocklist_extra_decoders = {"hardware.a"}});
  EXPECT_EQ(Names(SelectDecoderCandidates(Pool(), request)),
            (Names_t{"bundled.a", "hardware.b", "platform.a", "bundled.b"}));
}

TEST(DecoderSelectionTest, UnknownPreferredNamesAreIgnored) {
  auto request = Request({.preferred_decoders = {"missing"}});
  EXPECT_EQ(SelectDecoderCandidates(Pool(), request).size(), 5U);
}

TEST(BlocklistTest, MatchesVersionRangesInclusively) {
  const DeviceProfile device{.driver_version = "1.0.0"};
  EXPECT_NE(
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264, device, {}),
      nullptr);
  const DeviceProfile edge{.driver_version = "2.5"};
  EXPECT_NE(
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264, edge, {}),
      nullptr);
  const DeviceProfile above{.driver_version = "2.5.1"};
  EXPECT_EQ(
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264, above, {}),
      nullptr);
}

TEST(BlocklistTest, CodecAndDecoderMustMatch) {
  const DeviceProfile device{.driver_version = "2.0"};
  EXPECT_EQ(
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kAv1, device, {}),
      nullptr);
  EXPECT_EQ(
      FindBlocklistEntry(kBlocklist, "hardware.b", CodecId::kH264, device, {}),
      nullptr);
}

TEST(BlocklistTest, UnknownOrMalformedDeviceValuesDoNotMatchConstraints) {
  EXPECT_EQ(
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264, {}, {}),
      nullptr);
  const DeviceProfile malformed{.driver_version = "two"};
  EXPECT_EQ(FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264,
                               malformed, {}),
            nullptr);
  const DeviceProfile other_model{
      .port = "android", .os_version = "12", .device_model = "Model Y"};
  EXPECT_EQ(FindBlocklistEntry(kBlocklist, "platform.a", CodecId::kH264,
                               other_model, {}),
            nullptr);
}

TEST(BlocklistTest, ReturnsTheMatchingEntry) {
  const DeviceProfile device{.driver_version = "2.0"};
  const auto* entry =
      FindBlocklistEntry(kBlocklist, "hardware.a", CodecId::kH264, device, {});
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->id, "hardware-a-old-driver");
  EXPECT_EQ(entry->reason, "Corrupt output");
}

}
}
