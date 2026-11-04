#include "umbra/player/text_track_routing.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/player/text_track.h"

namespace umbra::player::internal {
namespace {

TextCue Cue(double start, double end) {
  TextCue cue;
  cue.start_time = start;
  cue.end_time = end;
  return cue;
}

std::vector<uint8_t> Box(std::string_view type,
                         const std::vector<uint8_t>& payload) {
  const auto size = static_cast<uint32_t>(payload.size() + 8);
  std::vector<uint8_t> bytes = {
      static_cast<uint8_t>(size >> 24), static_cast<uint8_t>(size >> 16),
      static_cast<uint8_t>(size >> 8),  static_cast<uint8_t>(size),
      static_cast<uint8_t>(type[0]),    static_cast<uint8_t>(type[1]),
      static_cast<uint8_t>(type[2]),    static_cast<uint8_t>(type[3])};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

penumbra::EncodedSample TextSample(std::vector<uint8_t> bytes) {
  penumbra::EncodedSample sample;
  sample.track_id = 1;
  sample.data = penumbra::SharedBytes::Copy(bytes);
  sample.pts.value = 2000;
  sample.pts.time_base = penumbra::Rational::Of(1, 1000);
  sample.duration.value = 1000;
  sample.duration.time_base = penumbra::Rational::Of(1, 1000);
  return sample;
}

}

TEST(TextTrackRoutingTest, SniffsFormatsFromContent) {
  EXPECT_EQ(SniffTextFormat("WEBVTT\n"), TextFormat::kWebVtt);
  EXPECT_EQ(SniffTextFormat("1\n00:00:01,000 --> 00:00:02,000\n"),
            TextFormat::kSubRip);
  EXPECT_EQ(SniffTextFormat("[Script Info]\n"), TextFormat::kAss);
  EXPECT_EQ(SniffTextFormat("<?xml version=\"1.0\"?><tt></tt>"),
            TextFormat::kTtml);
  EXPECT_EQ(SniffTextFormat("not a subtitle"), std::nullopt);
}

TEST(TextTrackRoutingTest, ParsesTimestampMap) {
  const auto map = ParseWebVttTimestampMap(
      "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:01.000,MPEGTS:90000\n\n");
  ASSERT_TRUE(map.has_value());
  EXPECT_DOUBLE_EQ(map->local_seconds, 1.0);
  EXPECT_EQ(map->mpegts, 90000U);
}

TEST(TextTrackRoutingTest, MapsWithoutHeaderRelativeToSegment) {
  std::vector<TextCue> cues = {Cue(1.0, 2.0)};
  ASSERT_TRUE(MapWebVttCues(cues, std::nullopt, 10.0));
  EXPECT_DOUBLE_EQ(cues.front().start_time, 11.0);
  EXPECT_DOUBLE_EQ(cues.front().end_time, 12.0);
}

TEST(TextTrackRoutingTest, MapsTimestampAcrossPtsRollover) {
  constexpr std::uint64_t kModulus = std::uint64_t{1} << 33;
  const double segment_start = static_cast<double>(kModulus - 45000) / 90000.0;
  std::vector<TextCue> cues = {Cue(0.0, 1.0)};
  ASSERT_TRUE(MapWebVttCues(
      cues, WebVttTimestampMap{.local_seconds = 0.0, .mpegts = 45000},
      segment_start));
  EXPECT_NEAR(cues.front().start_time,
              static_cast<double>(kModulus + 45000) / 90000.0, 0.00001);
}

TEST(TextTrackRoutingTest, RoutesPayloadBySignatureInsteadOfFileExtension) {
  TextTrack track(TextTrackKind::kSubtitles, "English", "en", 4096);
  const std::string payload = "1\n00:00:01,000 --> 00:00:02,000\nA subtitle\n";
  ASSERT_TRUE(LoadTextTrackPayload(track, payload, "windows-1252", true));
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_EQ(track.cues().front().runs.front().text, "A subtitle");
}

TEST(TextTrackRoutingTest, RoutesWebVttAndAppliesItsTimestampMap) {
  TextTrack track(TextTrackKind::kSubtitles, "English", "en", 4096);
  const std::string payload =
      "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:01.000,MPEGTS:90000\n\n"
      "00:00:02.000 --> 00:00:03.000\nA subtitle\n";
  ASSERT_TRUE(LoadTextTrackPayload(track, payload, "windows-1252", true));
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_DOUBLE_EQ(track.cues().front().start_time, 2.0);
  EXPECT_DOUBLE_EQ(track.cues().front().end_time, 3.0);
}

TEST(TextTrackRoutingTest, RoutesTx3gSampleToTimedCue) {
  TextTrack track(TextTrackKind::kSubtitles, "tx3g", "en", 4096);
  penumbra::StreamInfo stream;
  stream.track_id = 1;
  stream.codec_string = "tx3g";
  stream.time_base = penumbra::Rational::Of(1, 1000);
  stream.format = penumbra::TextInfo{};
  ASSERT_TRUE(LoadTextTrackSample(track, stream,
                                  TextSample({0, 4, 't', 'e', 's', 't'})));
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_DOUBLE_EQ(track.cues().front().start_time, 2.0);
  EXPECT_DOUBLE_EQ(track.cues().front().end_time, 3.0);
  EXPECT_EQ(track.cues().front().runs.front().text, "test");
}

TEST(TextTrackRoutingTest, RoutesWvttSampleToTimedCue) {
  TextTrack track(TextTrackKind::kSubtitles, "wvtt", "en", 4096);
  penumbra::StreamInfo stream;
  stream.track_id = 1;
  stream.codec_string = "wvtt";
  stream.time_base = penumbra::Rational::Of(1, 1000);
  stream.format = penumbra::TextInfo{};
  const std::vector<uint8_t> payload =
      Box("vttc", Box("payl", {'w', 'v', 't', 't'}));
  ASSERT_TRUE(LoadTextTrackSample(track, stream, TextSample(payload)));
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_DOUBLE_EQ(track.cues().front().start_time, 2.0);
  EXPECT_DOUBLE_EQ(track.cues().front().end_time, 3.0);
  EXPECT_EQ(track.cues().front().runs.front().text, "wvtt");
}

}
