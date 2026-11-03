#include "umbra/demux/packed_audio_demuxer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"

namespace umbra::demux {
namespace {

std::vector<uint8_t> AdtsFrame() {
  return {0xff, 0xf1, 0x50, 0x80, 0x01, 0x1f, 0xfc, 0x00};
}

std::vector<uint8_t> Mp3Frame() {
  std::vector<uint8_t> frame(417, 0);
  frame[0] = 0xff;
  frame[1] = 0xfb;
  frame[2] = 0x90;
  frame[3] = 0x64;
  return frame;
}

std::vector<uint8_t> PackedTimestampTag(uint64_t timestamp) {
  constexpr std::string_view owner =
      "com.apple.streaming.transportStreamTimestamp";
  std::vector<uint8_t> body = {'P', 'R', 'I', 'V'};
  const size_t frame_size = owner.size() + 9;
  body.push_back(static_cast<uint8_t>((frame_size >> 21) & 0x7fU));
  body.push_back(static_cast<uint8_t>((frame_size >> 14) & 0x7fU));
  body.push_back(static_cast<uint8_t>((frame_size >> 7) & 0x7fU));
  body.push_back(static_cast<uint8_t>(frame_size & 0x7fU));
  body.insert(body.end(), {0, 0});
  body.insert(body.end(), owner.begin(), owner.end());
  body.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8) {
    body.push_back(static_cast<uint8_t>(timestamp >> shift));
  }
  std::vector<uint8_t> tag = {'I', 'D', '3', 4, 0, 0};
  tag.push_back(static_cast<uint8_t>((body.size() >> 21) & 0x7fU));
  tag.push_back(static_cast<uint8_t>((body.size() >> 14) & 0x7fU));
  tag.push_back(static_cast<uint8_t>((body.size() >> 7) & 0x7fU));
  tag.push_back(static_cast<uint8_t>(body.size() & 0x7fU));
  tag.insert(tag.end(), body.begin(), body.end());
  return tag;
}

std::vector<uint8_t> Ac3Frame() {
  std::vector<uint8_t> frame = {0x0b, 0x77, 0x00, 0x00, 0x0a, 0x40, 0xe1};
  frame.resize(320, 0);
  return frame;
}

std::vector<uint8_t> Eac3Frame() {
  return {0x0b, 0x77, 0x00, 0x03, 0x00, 0x58, 0x00, 0x00};
}

std::vector<uint8_t> XingFrame() {
  std::vector<uint8_t> frame = Mp3Frame();
  frame[36] = 'X';
  frame[37] = 'i';
  frame[38] = 'n';
  frame[39] = 'g';
  frame[43] = 1;
  frame[47] = 3;
  frame[48] = 'L';
  frame[49] = 'A';
  frame[50] = 'M';
  frame[51] = 'E';
  frame[69] = 0x24;
  frame[70] = 0;
  frame[71] = 100;
  return frame;
}

std::vector<uint8_t> VbriFrame() {
  std::vector<uint8_t> frame = Mp3Frame();
  frame[36] = 'V';
  frame[37] = 'B';
  frame[38] = 'R';
  frame[39] = 'I';
  frame[53] = 3;
  return frame;
}

void Append(std::vector<uint8_t>& destination,
            const std::vector<uint8_t>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<uint8_t> ReadFixture(const std::string& name) {
  std::ifstream stream(std::string(ALUMBRA_DEMUX_FIXTURE_DIR) + "/" + name,
                       std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string Render(const test::RecordingOutput& output) {
  std::string text;
  for (size_t index = 0; index < output.streams.size(); ++index) {
    const penumbra::StreamInfo& stream = output.streams[index];
    text += "stream " + std::to_string(index) + " " +
            std::string(penumbra::CodecName(stream.codec)) + " " +
            stream.time_base.ToString() + "\n";
    for (const penumbra::EncodedSample& sample : output.samples) {
      text += std::to_string(sample.pts.value) + " " +
              std::to_string(sample.dts.value) + " " +
              std::to_string(sample.duration.value) + " " +
              std::to_string(sample.data.size()) + " " +
              (sample.key_frame ? "K\n" : "_\n");
    }
  }
  return text;
}

std::string ReadGolden(const std::string& name) {
  const std::vector<uint8_t> bytes = ReadFixture(name + ".packets");
  return {bytes.begin(), bytes.end()};
}

TEST(PackedAudioDemuxerTest, ParsesAdtsAndBuildsAudioSpecificConfig) {
  std::vector<uint8_t> bytes;
  Append(bytes, AdtsFrame());
  Append(bytes, AdtsFrame());
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 1);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.streams.size(), 1U);
  EXPECT_EQ(output.streams.front().codec, penumbra::CodecId::kAac);
  ASSERT_EQ(output.streams.front().codec_config.size(), 2U);
  EXPECT_TRUE(std::ranges::equal(output.streams.front().codec_config.span(),
                                 std::array<uint8_t, 2>{0x12, 0x10}));
  ASSERT_EQ(output.samples.size(), 2U);
  EXPECT_EQ(output.samples[0].pts.value, 0);
  EXPECT_EQ(output.samples[0].duration.value, 1024);
  EXPECT_EQ(output.samples[1].pts.value, 1024);
  ASSERT_NE(output.seek_map, nullptr);
  EXPECT_EQ(output.seek_map->Duration(),
            penumbra::TimeDelta::FromMicroseconds(46440));
  const auto seek = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMicroseconds(30000));
  ASSERT_TRUE(seek.has_value());
  EXPECT_EQ(seek->byte_offset, 8U);
}

TEST(PackedAudioDemuxerTest, EmitsId3WithoutChangingAudioTimestamps) {
  std::vector<uint8_t> bytes = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0};
  Append(bytes, AdtsFrame());
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 3);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.items.size(), 1U);
  EXPECT_EQ(output.items.front().payload.size(), 10U);
  ASSERT_EQ(output.samples.size(), 1U);
  EXPECT_EQ(output.samples.front().pts.value, 0);
  EXPECT_EQ(output.samples.front().duration.value, 1024);
}

TEST(PackedAudioDemuxerTest, UsesId3PrivateTimestampForFirstFrame) {
  std::vector<uint8_t> bytes = PackedTimestampTag(900000);
  Append(bytes, AdtsFrame());
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 5);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.samples.size(), 1U);
  EXPECT_EQ(output.samples.front().pts.value, 441000);
  EXPECT_EQ(output.samples.front().duration.value, 1024);
}

TEST(PackedAudioDemuxerTest, UnwrapsId3PrivateTimestampAcrossSegments) {
  const uint64_t wrap = uint64_t{1} << 33;
  std::vector<uint8_t> first = PackedTimestampTag(wrap - 45000);
  Append(first, AdtsFrame());
  std::vector<uint8_t> second = PackedTimestampTag(45000);
  Append(second, AdtsFrame());
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  ASSERT_TRUE(test::DriveBytes(demuxer, first, first.size()).status.ok());
  demuxer.Reset(0);
  ASSERT_TRUE(test::DriveBytes(demuxer, second, second.size()).status.ok());
  ASSERT_EQ(output.samples.size(), 2U);
  EXPECT_EQ(output.samples[1].pts.value - output.samples[0].pts.value, 44100);
}

TEST(PackedAudioDemuxerTest, ParsesDolbyPackedAudioFrames) {
  std::vector<uint8_t> ac3_bytes;
  Append(ac3_bytes, Ac3Frame());
  Append(ac3_bytes, Ac3Frame());
  test::RecordingOutput ac3_output;
  Ac3Demuxer ac3_demuxer(ac3_output);
  const test::DriveResult ac3_result =
      test::DriveBytes(ac3_demuxer, ac3_bytes, 37);
  ASSERT_TRUE(ac3_result.status.ok()) << ac3_result.status;
  ASSERT_EQ(ac3_output.samples.size(), 2U);
  EXPECT_EQ(ac3_output.streams.front().codec, penumbra::CodecId::kAc3);
  EXPECT_EQ(ac3_output.samples[1].pts.value, 1536);

  std::vector<uint8_t> eac3_bytes;
  Append(eac3_bytes, Eac3Frame());
  Append(eac3_bytes, Eac3Frame());
  test::RecordingOutput eac3_output;
  Eac3Demuxer eac3_demuxer(eac3_output);
  const test::DriveResult eac3_result =
      test::DriveBytes(eac3_demuxer, eac3_bytes, 3);
  ASSERT_TRUE(eac3_result.status.ok()) << eac3_result.status;
  ASSERT_EQ(eac3_output.samples.size(), 2U);
  EXPECT_EQ(eac3_output.streams.front().codec, penumbra::CodecId::kEac3);
  EXPECT_EQ(eac3_output.samples[1].pts.value, 256);
}

TEST(PackedAudioDemuxerTest, ParsesMp3FramesAndSupportsFrameSeek) {
  std::vector<uint8_t> bytes;
  Append(bytes, Mp3Frame());
  Append(bytes, Mp3Frame());
  Append(bytes, Mp3Frame());
  test::RecordingOutput output;
  Mp3Demuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 23);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.samples.size(), 3U);
  EXPECT_EQ(output.streams.front().codec, penumbra::CodecId::kMp3);
  EXPECT_EQ(output.samples[2].pts.value, 2304);
  ASSERT_NE(output.seek_map, nullptr);
  const auto point =
      output.seek_map->SeekPointFor(penumbra::MediaTime::FromMilliseconds(30));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, 417U);
  const auto offset = demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok()) << offset.status();
  EXPECT_EQ(*offset, 417U);
}

TEST(PackedAudioDemuxerTest, UsesXingDurationAndLameTrims) {
  std::vector<uint8_t> bytes;
  Append(bytes, XingFrame());
  Append(bytes, Mp3Frame());
  Append(bytes, Mp3Frame());
  Append(bytes, Mp3Frame());
  test::RecordingOutput output;
  Mp3Demuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 127);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.samples.size(), 3U);
  EXPECT_EQ(output.samples.front().trim_start.value, 576);
  EXPECT_EQ(output.samples.back().trim_end.value, 100);
  EXPECT_EQ(output.seek_map->Duration(),
            penumbra::TimeDelta::FromMicroseconds(63039));
}

TEST(PackedAudioDemuxerTest, DetectsAnId3v1Trailer) {
  std::vector<uint8_t> bytes;
  Append(bytes, AdtsFrame());
  std::vector<uint8_t> trailer(128, 0);
  trailer[0] = 'T';
  trailer[1] = 'A';
  trailer[2] = 'G';
  Append(bytes, trailer);
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 19);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.items.size(), 1U);
  EXPECT_EQ(output.items.front().payload.size(), 128U);
}

TEST(PackedAudioDemuxerTest, DetectsAnId3v1TrailerWithoutKnownSourceSize) {
  std::vector<uint8_t> bytes;
  Append(bytes, AdtsFrame());
  std::vector<uint8_t> trailer(128, 0);
  trailer[0] = 'T';
  trailer[1] = 'A';
  trailer[2] = 'G';
  Append(bytes, trailer);
  test::RecordingOutput output;
  AdtsDemuxer demuxer(output);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 19);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.items.size(), 1U);
  EXPECT_EQ(output.items.front().payload.size(), 128U);
}

TEST(PackedAudioDemuxerTest, UsesVbriFrameCountForDurationAndSeeking) {
  std::vector<uint8_t> bytes;
  Append(bytes, VbriFrame());
  Append(bytes, Mp3Frame());
  Append(bytes, Mp3Frame());
  test::RecordingOutput output;
  Mp3Demuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  const test::DriveResult result = test::DriveBytes(demuxer, bytes, 97);
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_EQ(output.samples.size(), 2U);
  EXPECT_EQ(output.seek_map->Duration(),
            penumbra::TimeDelta::FromMicroseconds(78367));
  const auto point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMicroseconds(40000));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, 834U);
  EXPECT_LT(40000 - point->time.InMicroseconds(), 26200);
}

TEST(PackedAudioGoldenTest, AdtsPacketsMatchTheGoldenTable) {
  const std::vector<uint8_t> bytes = ReadFixture("packed_aac.aac");
  ASSERT_FALSE(bytes.empty());
  const std::string golden = ReadGolden("packed_aac.aac");
  for (const size_t chunk : {size_t{1}, size_t{1500}}) {
    test::RecordingOutput output;
    AdtsDemuxer demuxer(output);
    demuxer.SetSourceInfo(bytes.size(), true);
    ASSERT_TRUE(test::DriveBytes(demuxer, bytes, chunk).status.ok());
    EXPECT_EQ(Render(output), golden);
    EXPECT_EQ(output.seek_map->Duration(),
              penumbra::TimeDelta::FromMicroseconds(325079));
    const auto point = output.seek_map->SeekPointFor(
        penumbra::MediaTime::FromMicroseconds(150000));
    ASSERT_TRUE(point.has_value());
    EXPECT_LT(150000 - point->time.InMicroseconds(), 24000);
  }
}

TEST(PackedAudioGoldenTest, Mp3PacketsMatchTheGoldenTableAndGaplessDuration) {
  const std::vector<uint8_t> bytes = ReadFixture("packed_mp3.mp3");
  ASSERT_FALSE(bytes.empty());
  const std::string golden = ReadGolden("packed_mp3.mp3");
  for (const size_t chunk : {size_t{1}, size_t{1500}}) {
    test::RecordingOutput output;
    Mp3Demuxer demuxer(output);
    demuxer.SetSourceInfo(bytes.size(), true);
    ASSERT_TRUE(test::DriveBytes(demuxer, bytes, chunk).status.ok());
    EXPECT_EQ(Render(output), golden);
    EXPECT_EQ(output.seek_map->Duration(),
              penumbra::TimeDelta::FromMicroseconds(300000));
    const auto point = output.seek_map->SeekPointFor(
        penumbra::MediaTime::FromMicroseconds(150000));
    ASSERT_TRUE(point.has_value());
    EXPECT_LT(150000 - point->time.InMicroseconds(), 27000);
  }
}

}
}
