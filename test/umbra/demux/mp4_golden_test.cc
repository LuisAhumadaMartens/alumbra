#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "umbra/demux/mp4_demuxer.h"

namespace umbra::demux::test {
namespace {

Bytes ReadFile(const std::string& name) {
  std::ifstream stream(std::string(ALUMBRA_DEMUX_FIXTURE_DIR) + "/" + name,
                       std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string ReadText(const std::string& name) {
  const Bytes bytes = ReadFile(name);
  return {bytes.begin(), bytes.end()};
}

std::string Render(const RecordingOutput& output) {
  std::string text;
  for (size_t index = 0; index < output.streams.size(); ++index) {
    const penumbra::StreamInfo& stream = output.streams[index];
    absl::StrAppend(&text, "stream ", index, " ",
                    penumbra::CodecName(stream.codec), " ",
                    stream.time_base.ToString(), "\n");
    for (const penumbra::EncodedSample& sample :
         output.SamplesOf(stream.track_id)) {
      absl::StrAppend(&text, sample.pts.value, " ", sample.dts.value, " ",
                      sample.duration.value, " ", sample.data.size(), " ",
                      sample.key_frame ? "K" : "_", "\n");
    }
  }
  return text;
}

class GoldenTest : public ::testing::TestWithParam<const char*> {};

TEST_P(GoldenTest, PacketsMatchFfprobe) {
  const std::string name = GetParam();
  const Bytes file = ReadFile(name);
  ASSERT_FALSE(file.empty());
  const std::string golden = ReadText(name + ".packets");
  ASSERT_FALSE(golden.empty());
  for (const size_t chunk : {size_t{1} << 20, size_t{1500}}) {
    RecordingOutput output;
    Mp4Demuxer demuxer(output);
    const DriveResult result = DriveBytes(demuxer, file, chunk);
    ASSERT_TRUE(result.status.ok()) << result.status;
    EXPECT_EQ(Render(output), golden) << "chunk " << chunk;
  }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, GoldenTest,
                         ::testing::Values("progressive_moov_end.mp4",
                                           "progressive_faststart.mp4",
                                           "fragmented_sidx.mp4",
                                           "audio_priming.m4a",
                                           "multi_codec.mp4"));

TEST(GoldenFixtureTest, AacPrimingIsTrimmedSoThePresentationStartsAtZero) {
  const Bytes file = ReadFile("audio_priming.m4a");
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_GE(output.samples.size(), 2U);
  const penumbra::EncodedSample& first = output.samples[0];
  EXPECT_EQ(first.pts.value, -1024);
  EXPECT_EQ(first.trim_start.value, 1024);
  EXPECT_EQ(first.pts.value + first.trim_start.value, 0);
  EXPECT_EQ(output.samples[1].pts.value, 0);
  EXPECT_FALSE(output.samples[1].trim_start.is_known());
}

TEST(GoldenFixtureTest, IgnoringEditListsKeepsTheMediaTimeline) {
  const Bytes file = ReadFile("audio_priming.m4a");
  RecordingOutput output;
  Mp4DemuxerOptions options;
  options.edit_lists_enabled = false;
  Mp4Demuxer demuxer(output, options);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  EXPECT_EQ(output.samples[0].pts.value, 0);
  EXPECT_FALSE(output.samples[0].trim_start.is_known());
}

TEST(GoldenFixtureTest, EveryCodecIsDescribed) {
  const Bytes file = ReadFile("multi_codec.mp4");
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_EQ(output.streams.size(), 6U);
  const std::vector<std::string> prefixes = {"hvc1.", "vp09.", "av01.",
                                             "opus",  "ac-3",  "ec-3"};
  for (size_t index = 0; index < prefixes.size(); ++index) {
    EXPECT_EQ(output.streams[index].codec_string.rfind(prefixes[index], 0), 0U)
        << output.streams[index].codec_string;
    EXPECT_FALSE(output.streams[index].codec_config.empty()) << index;
    EXPECT_NE(output.streams[index].codec, penumbra::CodecId::kUnknown);
  }
  const auto* video =
      std::get_if<penumbra::VideoInfo>(&output.streams[0].format);
  ASSERT_NE(video, nullptr);
  EXPECT_EQ(video->coded_width, 64);
  EXPECT_EQ(video->coded_height, 64);
  const auto* audio =
      std::get_if<penumbra::AudioInfo>(&output.streams[3].format);
  ASSERT_NE(audio, nullptr);
  EXPECT_EQ(audio->channel_count, 1);
}

TEST(GoldenFixtureTest, SidxFragmentsSeekToTheirKeyFrames) {
  const Bytes file = ReadFile("fragmented_sidx.mp4");
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_NE(output.seek_map, nullptr);
  ASSERT_TRUE(output.seek_map->IsSeekable());
  const auto point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(1500));
  ASSERT_TRUE(point.has_value());
  EXPECT_GT(point->byte_offset, 0U);
  EXPECT_LE(point->time, penumbra::MediaTime::FromMilliseconds(1500));
  output.samples.clear();
  ASSERT_TRUE(demuxer.Seek(*point).ok());
  ASSERT_TRUE(DriveBytes(demuxer, file, 4096).status.ok());
  ASSERT_FALSE(output.samples.empty());
  EXPECT_TRUE(output.samples[0].discontinuity);
}

}
}
