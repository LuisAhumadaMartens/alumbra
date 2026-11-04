#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "umbra/demux/mkv_demuxer.h"

namespace umbra::demux::test {
namespace {

struct GoldenPacket {
  int64_t pts = 0;
  std::optional<int64_t> duration;
  size_t size = 0;
  bool key = false;
};

struct GoldenStream {
  std::string codec;
  std::vector<GoldenPacket> packets;
};

Bytes ReadFile(const std::string& name) {
  std::ifstream stream(std::string(ALUMBRA_DEMUX_FIXTURE_DIR) + "/" + name,
                       std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::vector<GoldenStream> ReadGolden(const std::string& name) {
  const Bytes bytes = ReadFile(name);
  std::istringstream lines(std::string(bytes.begin(), bytes.end()));
  std::vector<GoldenStream> streams;
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string first;
    fields >> first;
    if (first == "stream") {
      std::string index;
      GoldenStream stream;
      fields >> index >> stream.codec;
      streams.push_back(stream);
      continue;
    }
    std::string dts;
    std::string duration;
    std::string key;
    GoldenPacket packet;
    packet.pts = std::stoll(first);
    fields >> dts >> duration >> packet.size >> key;
    if (duration != "NA") {
      packet.duration = std::stoll(duration);
    }
    packet.key = key == "K";
    streams.back().packets.push_back(packet);
  }
  return streams;
}

int64_t ToMilliseconds(const penumbra::Timestamp& timestamp) {
  return penumbra::Rescale(timestamp.value, timestamp.time_base,
                           penumbra::Rational::Of(1, 1000),
                           penumbra::Rounding::kNearest)
      .value_or(0);
}

class MatroskaGoldenTest : public ::testing::TestWithParam<const char*> {};

TEST_P(MatroskaGoldenTest, PacketsMatchFfprobe) {
  const std::string name = GetParam();
  const Bytes file = ReadFile(name);
  ASSERT_FALSE(file.empty());
  const std::vector<GoldenStream> golden = ReadGolden(name + ".packets");
  ASSERT_FALSE(golden.empty());
  for (const size_t chunk : {size_t{1} << 20, size_t{1500}, size_t{1}}) {
    RecordingOutput output;
    MatroskaDemuxer demuxer(output);
    const DriveResult result = DriveBytes(demuxer, file, chunk);
    ASSERT_TRUE(result.status.ok()) << result.status << " chunk " << chunk;
    ASSERT_EQ(output.streams.size(), golden.size());
    for (size_t index = 0; index < golden.size(); ++index) {
      const auto samples = output.SamplesOf(output.streams[index].track_id);
      const std::vector<GoldenPacket>& expected = golden[index].packets;
      ASSERT_EQ(samples.size(), expected.size())
          << penumbra::CodecName(output.streams[index].codec);
      for (size_t packet = 0; packet < samples.size(); ++packet) {
        const std::string where =
            std::string(penumbra::CodecName(output.streams[index].codec)) +
            " packet " + std::to_string(packet) + " chunk " +
            std::to_string(chunk);
        const int64_t tolerance =
            output.streams[index].codec == penumbra::CodecId::kOpus ? 1 : 0;
        EXPECT_NEAR(ToMilliseconds(samples[packet].pts), expected[packet].pts,
                    static_cast<double>(tolerance))
            << where;
        EXPECT_EQ(samples[packet].data.size(), expected[packet].size) << where;
        EXPECT_EQ(samples[packet].key_frame, expected[packet].key) << where;
        if (packet + 1 < samples.size() &&
            samples[packet].duration.is_known() &&
            expected[packet].duration.has_value()) {
          EXPECT_EQ(ToMilliseconds(samples[packet].duration),
                    *expected[packet].duration)
              << where;
        }
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, MatroskaGoldenTest,
                         ::testing::Values("webm_vp9_opus.webm",
                                           "webm_vp8_vorbis.webm",
                                           "webm_unknown_size.webm",
                                           "matroska_multi_codec.mkv"));

TEST(MatroskaGoldenFixtureTest, EveryCodecIsDescribed) {
  const Bytes file = ReadFile("matroska_multi_codec.mkv");
  RecordingOutput output;
  MatroskaDemuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_EQ(output.streams.size(), 10U);
  const std::vector<std::string> prefixes = {
      "avc1.", "hvc1.",   "vp09.", "av01.", "mp4a.40.",
      "fLaC",  "mp4a.6B", "ac-3",  "ec-3",  "opus"};
  for (size_t index = 0; index < prefixes.size(); ++index) {
    EXPECT_EQ(output.streams[index].codec_string.rfind(prefixes[index], 0), 0U)
        << output.streams[index].codec_string;
    EXPECT_NE(output.streams[index].codec, penumbra::CodecId::kUnknown);
  }
  EXPECT_EQ(output.streams[0].nal_unit_framing,
            penumbra::NalUnitFraming::kLengthPrefixed);
  EXPECT_GT(output.streams[0].nal_length_size, 0U);
  EXPECT_EQ(output.streams[1].nal_unit_framing,
            penumbra::NalUnitFraming::kLengthPrefixed);
  EXPECT_GT(output.streams[1].nal_length_size, 0U);
  EXPECT_EQ(output.streams[4].aac_framing, penumbra::AacFraming::kRaw);
  EXPECT_EQ(demuxer.Stats().skipped_tracks, 0U);
}

TEST(MatroskaGoldenFixtureTest, OpusCodecDelayPutsThePresentationStartAtZero) {
  const Bytes file = ReadFile("webm_vp9_opus.webm");
  RecordingOutput output;
  MatroskaDemuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  const auto audio = output.SamplesOf(output.streams[1].track_id);
  ASSERT_GE(audio.size(), 2U);
  EXPECT_EQ(audio[0].pts.value, -312);
  EXPECT_EQ(audio[0].pts.time_base, penumbra::Rational::Of(1, 48000));
  EXPECT_EQ(audio[0].trim_start.value, 312);
  EXPECT_EQ(audio[0].pts.value + audio[0].trim_start.value, 0);
  EXPECT_EQ(ToMilliseconds(audio[0].pts), -7);
  const auto& last = audio.back();
  EXPECT_EQ(last.trim_end.value, 648);
}

TEST(MatroskaGoldenFixtureTest, CuesGiveSeekPointsAtTheClusters) {
  const Bytes file = ReadFile("webm_vp9_opus.webm");
  RecordingOutput output;
  MatroskaDemuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_NE(output.seek_map, nullptr);
  ASSERT_TRUE(output.seek_map->IsSeekable());
  EXPECT_EQ(output.seek_map->Duration(),
            penumbra::TimeDelta::FromMilliseconds(2008));
  const auto point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(1500));
  ASSERT_TRUE(point.has_value());
  output.samples.clear();
  ASSERT_TRUE(demuxer.Seek(*point).ok());
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_FALSE(output.samples.empty());
  EXPECT_TRUE(output.samples[0].discontinuity);
  const auto video = output.SamplesOf(output.streams[0].track_id);
  ASSERT_FALSE(video.empty());
  EXPECT_TRUE(video[0].key_frame);
  EXPECT_LE(ToMilliseconds(video[0].pts), 1500);
}

TEST(MatroskaGoldenFixtureTest, TheLiveFileHasNoCuesButStillSeeks) {
  const Bytes file = ReadFile("webm_unknown_size.webm");
  RecordingOutput output;
  MatroskaDemuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_NE(output.seek_map, nullptr);
  ASSERT_TRUE(output.seek_map->IsSeekable());
  EXPECT_TRUE(output.seek_map->Duration().is_unknown());
  const auto point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(1800));
  ASSERT_TRUE(point.has_value());
  EXPECT_GT(point->byte_offset, 0U);
}

}
}
