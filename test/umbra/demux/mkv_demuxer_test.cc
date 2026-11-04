#include "umbra/demux/mkv_demuxer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mkv_builder.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux::test {
namespace {

constexpr uint64_t kVideo = 1;
constexpr uint64_t kAudio = 2;
constexpr uint64_t kFortyMilliseconds = 40000000;
constexpr uint64_t kTwentyMilliseconds = 20000000;
constexpr uint64_t kOpusDelay = 6500000;
constexpr size_t kWholeFile = size_t{1} << 24;

struct Session {
  Session() : demuxer(output) {}

  RecordingOutput output;
  MatroskaDemuxer demuxer;
};

Bytes OpusFrame(uint64_t track, size_t index, size_t size = 40) {
  Bytes frame = FrameBytes(track, index, size);
  frame[0] = 0xF8;
  return frame;
}

MkvTrackSpec VideoTrack(uint64_t number = kVideo) {
  MkvTrackSpec track;
  track.number = number;
  track.codec = MkvTestCodec::kVp9;
  track.default_duration_ns = kFortyMilliseconds;
  return track;
}

MkvTrackSpec OpusTrack(uint64_t number = kAudio) {
  MkvTrackSpec track;
  track.number = number;
  track.codec = MkvTestCodec::kOpus;
  track.codec_delay_ns = kOpusDelay;
  track.seek_pre_roll_ns = 80000000;
  return track;
}

MkvBlockSpec VideoBlock(size_t index, int16_t ticks, bool key) {
  MkvBlockSpec block;
  block.track = kVideo;
  block.relative_ticks = ticks;
  block.key = key;
  block.frames = {FrameBytes(kVideo, index, 100 + index)};
  return block;
}

MkvBlockSpec AudioBlock(size_t index, int16_t ticks) {
  MkvBlockSpec block;
  block.track = kAudio;
  block.relative_ticks = ticks;
  block.frames = {OpusFrame(kAudio, index)};
  return block;
}

MkvFileSpec BasicSpec(size_t clusters = 3) {
  MkvFileSpec spec;
  spec.duration = 3000;
  spec.tracks = {VideoTrack(), OpusTrack()};
  size_t index = 0;
  for (size_t cluster = 0; cluster < clusters; ++cluster) {
    MkvClusterSpec item;
    item.timestamp = cluster * 1000;
    for (int16_t tick = 0; tick < 1000; tick += 200) {
      item.blocks.push_back(VideoBlock(index, tick, tick == 0));
      item.blocks.push_back(AudioBlock(index, tick));
      ++index;
    }
    spec.clusters.push_back(item);
  }
  return spec;
}

absl::Status Feed(Session& session, const Bytes& bytes, size_t chunk) {
  return DriveBytes(session.demuxer, bytes, chunk).status;
}

std::string Describe(const RecordingOutput& output) {
  std::string text;
  for (const penumbra::EncodedSample& sample : output.samples) {
    text += sample.ToString();
    text += " trim=" + sample.trim_start.ToString() + "/" +
            sample.trim_end.ToString() + "\n";
  }
  return text;
}

TEST(MatroskaDemuxerTest, DescribesTheStreams) {
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(BasicSpec()).bytes, kWholeFile).ok());
  const RecordingOutput& output = session.output;
  ASSERT_EQ(output.streams.size(), 2U);
  const penumbra::StreamInfo& video = output.streams[0];
  EXPECT_EQ(video.track_id, 1U);
  EXPECT_EQ(video.codec, penumbra::CodecId::kVp9);
  EXPECT_EQ(video.time_base, penumbra::Rational::Of(1, 1000));
  EXPECT_EQ(video.codec_string.rfind("vp09.", 0), 0U) << video.codec_string;
  EXPECT_FALSE(video.codec_config.empty());
  EXPECT_EQ(video.language, "eng");
  const auto* video_info = std::get_if<penumbra::VideoInfo>(&video.format);
  ASSERT_NE(video_info, nullptr);
  EXPECT_EQ(video_info->coded_width, 160);
  EXPECT_EQ(video_info->coded_height, 90);
  EXPECT_EQ(video_info->frame_rate, penumbra::Rational::Of(25, 1));
  const penumbra::StreamInfo& audio = output.streams[1];
  EXPECT_EQ(audio.codec, penumbra::CodecId::kOpus);
  EXPECT_EQ(audio.time_base, penumbra::Rational::Of(1, 48000));
  EXPECT_EQ(audio.codec_string, "opus");
  const auto* audio_info = std::get_if<penumbra::AudioInfo>(&audio.format);
  ASSERT_NE(audio_info, nullptr);
  EXPECT_EQ(audio_info->channel_count, 2);
  EXPECT_EQ(audio_info->sample_rate, 48000);
  EXPECT_EQ(output.stream_announcements, 1U);
  ASSERT_NE(output.seek_map, nullptr);
  EXPECT_EQ(output.seek_map->Duration(), penumbra::TimeDelta::FromSeconds(3));
}

TEST(MatroskaDemuxerTest, DeliversExactTimestampsAndKeyFrames) {
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(BasicSpec()).bytes, kWholeFile).ok());
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 15U);
  for (size_t index = 0; index < video.size(); ++index) {
    EXPECT_EQ(video[index].pts.value, static_cast<int64_t>(index * 200));
    EXPECT_EQ(video[index].dts.value, video[index].pts.value);
    EXPECT_EQ(video[index].duration.value, 40);
    EXPECT_EQ(video[index].key_frame, index % 5 == 0) << index;
    EXPECT_EQ(video[index].data.size(), 100 + index);
    EXPECT_EQ(video[index].data.span().size(), 100 + index);
  }
  EXPECT_TRUE(video[0].discontinuity == false);
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_EQ(audio.size(), 15U);
  EXPECT_EQ(audio[0].pts.value, -312);
  EXPECT_EQ(audio[0].trim_start.value, 312);
  EXPECT_EQ(audio[0].pts.value + audio[0].trim_start.value, 0);
  EXPECT_EQ(audio[0].duration.value, 960);
  EXPECT_EQ(audio[1].pts.value, (200 * 48) - 312);
  EXPECT_FALSE(audio[1].trim_start.is_known());
  EXPECT_TRUE(audio[1].key_frame);
}

TEST(MatroskaDemuxerTest, PreservesTheFrameBytes) {
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(BasicSpec()).bytes, 7).ok());
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_GE(video.size(), 2U);
  for (size_t index = 0; index < video.size(); ++index) {
    const Bytes expected = FrameBytes(kVideo, index, 100 + index);
    ASSERT_EQ(video[index].data.size(), expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                           video[index].data.span().begin()))
        << index;
  }
}

TEST(MatroskaDemuxerTest, OneByteFeedingMatchesASingleChunk) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  spec.cues_at_end = true;
  const Bytes bytes = BuildMkv(spec).bytes;
  Session whole;
  ASSERT_TRUE(Feed(whole, bytes, kWholeFile).ok());
  for (const size_t chunk : {size_t{1}, size_t{3}, size_t{64}}) {
    Session pieces;
    ASSERT_TRUE(Feed(pieces, bytes, chunk).ok()) << chunk;
    EXPECT_EQ(Describe(pieces.output), Describe(whole.output)) << chunk;
  }
}

TEST(MatroskaDemuxerTest, ReadsBlockGroupsWithDurationAndReferences) {
  MkvFileSpec spec;
  spec.tracks = {VideoTrack()};
  MkvClusterSpec cluster;
  MkvBlockSpec key = VideoBlock(0, 0, true);
  key.group = true;
  key.duration = 33;
  MkvBlockSpec delta = VideoBlock(1, 33, false);
  delta.group = true;
  delta.reference = true;
  cluster.blocks = {key, delta, VideoBlock(2, 66, false)};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 5).ok());
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 3U);
  EXPECT_TRUE(video[0].key_frame);
  EXPECT_EQ(video[0].duration.value, 33);
  EXPECT_FALSE(video[1].key_frame);
  EXPECT_EQ(video[1].duration.value, 40);
  EXPECT_FALSE(video[2].key_frame);
}

TEST(MatroskaDemuxerTest, DiscardPaddingTrimsTheEndOfOpus) {
  MkvFileSpec spec;
  spec.tracks = {OpusTrack()};
  MkvClusterSpec cluster;
  cluster.blocks = {AudioBlock(0, 0), AudioBlock(1, 20)};
  cluster.blocks[1].group = true;
  cluster.blocks[1].discard_padding = 13500000;
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_EQ(audio.size(), 2U);
  EXPECT_FALSE(audio[0].trim_end.is_known());
  EXPECT_EQ(audio[1].trim_end.value, 648);
  EXPECT_EQ(audio[1].trim_end.time_base, penumbra::Rational::Of(1, 48000));
}

TEST(MatroskaDemuxerTest, NegativeDiscardPaddingTrimsTheStart) {
  MkvFileSpec spec;
  MkvTrackSpec track = OpusTrack();
  track.codec_delay_ns = 0;
  spec.tracks = {track};
  MkvClusterSpec cluster;
  cluster.blocks = {AudioBlock(0, 0)};
  cluster.blocks[0].group = true;
  cluster.blocks[0].discard_padding = -5000000;
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_EQ(audio.size(), 1U);
  EXPECT_EQ(audio[0].trim_start.value, 240);
}

class LacingTest : public ::testing::TestWithParam<MkvLacing> {};

TEST_P(LacingTest, SplitsOneBlockIntoFrames) {
  MkvFileSpec spec;
  MkvTrackSpec track = OpusTrack();
  track.codec_delay_ns = 0;
  track.default_duration_ns = kTwentyMilliseconds;
  spec.tracks = {track};
  MkvBlockSpec block = AudioBlock(0, 100);
  const std::array<size_t, 4> sizes = {40, 300, 20, 40};
  block.frames.clear();
  for (size_t index = 0; index < 4; ++index) {
    const size_t size = GetParam() == MkvLacing::kFixed ? 40 : sizes.at(index);
    block.frames.push_back(OpusFrame(kAudio, index, size));
  }
  block.lacing = GetParam();
  MkvClusterSpec cluster;
  cluster.timestamp = 1000;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  for (const size_t chunk : {kWholeFile, size_t{1}}) {
    Session session;
    ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, chunk).ok());
    const auto audio = session.output.SamplesOf(kAudio);
    ASSERT_EQ(audio.size(), 4U);
    for (size_t index = 0; index < audio.size(); ++index) {
      EXPECT_EQ(audio[index].pts.value,
                static_cast<int64_t>((size_t{1100} * 48) + (index * 960)))
          << index;
      EXPECT_EQ(audio[index].duration.value, 960);
      ASSERT_EQ(audio[index].data.size(), block.frames[index].size()) << index;
      EXPECT_TRUE(std::equal(block.frames[index].begin(),
                             block.frames[index].end(),
                             audio[index].data.span().begin()))
          << index;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Kinds, LacingTest,
                         ::testing::Values(MkvLacing::kXiph, MkvLacing::kEbml,
                                           MkvLacing::kFixed));

TEST(MatroskaDemuxerTest, LacedBlockWithoutDurationsIsCorrupt) {
  MkvFileSpec spec;
  MkvTrackSpec track;
  track.number = kAudio;
  track.codec = MkvTestCodec::kAc3;
  spec.tracks = {track};
  MkvBlockSpec block;
  block.track = kAudio;
  block.frames = {FrameBytes(kAudio, 0, 30), FrameBytes(kAudio, 1, 30)};
  block.lacing = MkvLacing::kFixed;
  MkvClusterSpec cluster;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  Session session;
  EXPECT_EQ(Feed(session, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kDataLoss);
}

TEST(MatroskaDemuxerTest, LacedBlockDurationIsSharedBetweenFrames) {
  MkvFileSpec spec;
  MkvTrackSpec track;
  track.number = kAudio;
  track.codec = MkvTestCodec::kAc3;
  spec.tracks = {track};
  MkvBlockSpec block;
  block.track = kAudio;
  block.frames = {FrameBytes(kAudio, 0, 30), FrameBytes(kAudio, 1, 30)};
  block.lacing = MkvLacing::kFixed;
  block.group = true;
  block.duration = 64;
  MkvClusterSpec cluster;
  cluster.timestamp = 10;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_EQ(audio.size(), 2U);
  EXPECT_EQ(audio[0].pts.value, 10);
  EXPECT_EQ(audio[0].duration.value, 32);
  EXPECT_EQ(audio[1].pts.value, 42);
  EXPECT_EQ(audio[1].duration.value, 32);
}

TEST(MatroskaDemuxerTest, LacedFramesKeepFractionalDurationsExact) {
  MkvFileSpec spec;
  MkvTrackSpec track;
  track.number = kAudio;
  track.codec = MkvTestCodec::kAac;
  track.default_duration_ns = 21333333;
  spec.tracks = {track};
  MkvBlockSpec block;
  block.track = kAudio;
  for (size_t index = 0; index < 4; ++index) {
    block.frames.push_back(FrameBytes(kAudio, index, 30));
  }
  block.lacing = MkvLacing::kFixed;
  MkvClusterSpec cluster;
  cluster.timestamp = 100;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_EQ(audio.size(), 4U);
  EXPECT_EQ(audio[0].pts.value, 100);
  EXPECT_EQ(audio[1].pts.value, 121);
  EXPECT_EQ(audio[2].pts.value, 143);
  EXPECT_EQ(audio[3].pts.value, 164);
  EXPECT_EQ(audio[3].duration.value, 21);
}

TEST(MatroskaDemuxerTest, UnknownSizeClustersAndSegmentPlay) {
  const MkvFileSpec known = BasicSpec();
  Session expected;
  ASSERT_TRUE(Feed(expected, BuildMkv(known).bytes, kWholeFile).ok());
  MkvFileSpec live = BasicSpec();
  live.unknown_segment_size = true;
  live.seek_head = false;
  for (MkvClusterSpec& cluster : live.clusters) {
    cluster.unknown_size = true;
  }
  for (const size_t chunk : {kWholeFile, size_t{1}, size_t{13}}) {
    Session session;
    ASSERT_TRUE(Feed(session, BuildMkv(live).bytes, chunk).ok()) << chunk;
    EXPECT_EQ(Describe(session.output), Describe(expected.output)) << chunk;
  }
}

TEST(MatroskaDemuxerTest, UnknownSizeClusterEndsAtTheCues) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  spec.cues_at_end = true;
  spec.seek_head = false;
  for (MkvClusterSpec& cluster : spec.clusters) {
    cluster.unknown_size = true;
  }
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 11).ok());
  EXPECT_EQ(session.output.samples.size(), 30U);
  ASSERT_NE(session.output.seek_map, nullptr);
  EXPECT_TRUE(session.output.seek_map->IsSeekable());
}

TEST(MatroskaDemuxerTest, CuesFarFromTheClustersAreFetchedWithTwoSeeks) {
  MkvFileSpec spec = BasicSpec();
  for (MkvClusterSpec& cluster : spec.clusters) {
    for (MkvBlockSpec& block : cluster.blocks) {
      block.frames[0].resize(70000, 7);
    }
  }
  spec.cues = true;
  spec.cues_at_end = true;
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  const DriveResult result = DriveBytes(session.demuxer, built.bytes, 4096);
  ASSERT_TRUE(result.status.ok()) << result.status;
  EXPECT_EQ(result.seek_requests, 2U);
  EXPECT_EQ(session.output.samples.size(), 30U);
  ASSERT_NE(session.output.seek_map, nullptr);
  EXPECT_TRUE(session.output.seek_map->IsSeekable());
  const auto point = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(2500));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, built.cluster_offsets[2]);
  EXPECT_EQ(point->time, penumbra::MediaTime::FromMilliseconds(2000));
}

TEST(MatroskaDemuxerTest, CuesAtTheEndOfASmallFileAreReadWithoutAFarSeek) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  spec.cues_at_end = true;
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  const DriveResult result = DriveBytes(session.demuxer, built.bytes, 4096);
  ASSERT_TRUE(result.status.ok()) << result.status;
  EXPECT_EQ(session.output.samples.size(), 30U);
  ASSERT_NE(session.output.seek_map, nullptr);
  EXPECT_TRUE(session.output.seek_map->IsSeekable());
}

TEST(MatroskaDemuxerTest, CuesBeforeTheClustersNeedNoSeek) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  const DriveResult result = DriveBytes(session.demuxer, built.bytes, 4096);
  ASSERT_TRUE(result.status.ok()) << result.status;
  EXPECT_EQ(result.seek_requests, 0U);
  ASSERT_NE(session.output.seek_map, nullptr);
  const auto point = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(1999));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, built.cluster_offsets[1]);
}

TEST(MatroskaDemuxerTest, ASourceThatCannotSeekIgnoresTheCuesPosition) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  spec.cues_at_end = true;
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  session.demuxer.SetSourceInfo(built.bytes.size(), false);
  size_t fed = 0;
  size_t seeks = 0;
  for (size_t guard = 0; guard < 100000; ++guard) {
    const auto step = session.demuxer.Step();
    ASSERT_TRUE(step.ok()) << step.status();
    if (step->kind == StepKind::kEndOfStream) {
      break;
    }
    if (step->kind == StepKind::kSeek) {
      ++seeks;
      continue;
    }
    if (step->kind == StepKind::kNeedMoreData) {
      const uint64_t offset = session.demuxer.NextOffset();
      if (offset >= built.bytes.size()) {
        session.demuxer.EndOfInput();
        continue;
      }
      const size_t count = std::min<size_t>(4096, built.bytes.size() - offset);
      ASSERT_TRUE(session.demuxer
                      .Append(offset, penumbra::SharedBytes::Copy(
                                          std::span<const uint8_t>(built.bytes)
                                              .subspan(offset, count)))
                      .ok());
      fed += count;
    }
  }
  EXPECT_EQ(seeks, 0U);
  EXPECT_EQ(fed, built.bytes.size());
  EXPECT_EQ(session.output.samples.size(), 30U);
  EXPECT_TRUE(session.output.seek_map->IsSeekable());
}

TEST(MatroskaDemuxerTest, SeekingWithCuesRestartsAtTheClusterAndFlags) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  ASSERT_TRUE(Feed(session, built.bytes, 4096).ok());
  const auto point = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(1500));
  ASSERT_TRUE(point.has_value());
  session.output.samples.clear();
  const absl::StatusOr<uint64_t> offset = session.demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok());
  EXPECT_EQ(*offset, built.cluster_offsets[1]);
  EXPECT_EQ(session.demuxer.NextOffset(), *offset);
  const DriveResult result = DriveBytes(session.demuxer, built.bytes, 4096);
  ASSERT_TRUE(result.status.ok()) << result.status;
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 10U);
  EXPECT_EQ(video[0].pts.value, 1000);
  EXPECT_TRUE(video[0].discontinuity);
  EXPECT_FALSE(video[1].discontinuity);
  const auto audio = session.output.SamplesOf(kAudio);
  ASSERT_FALSE(audio.empty());
  EXPECT_TRUE(audio[0].discontinuity);
}

TEST(MatroskaDemuxerTest, WithoutCuesTheSeekMapGrowsWithTheClusters) {
  const BuiltMkv built = BuildMkv(BasicSpec(5));
  Session session;
  ASSERT_TRUE(Feed(session, built.bytes, 100).ok());
  ASSERT_NE(session.output.seek_map, nullptr);
  ASSERT_TRUE(session.output.seek_map->IsSeekable());
  const auto point = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(3300));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, built.cluster_offsets[3]);
  EXPECT_EQ(point->time, penumbra::MediaTime::FromMilliseconds(3000));
  session.output.samples.clear();
  ASSERT_TRUE(session.demuxer.Seek(*point).ok());
  ASSERT_TRUE(Feed(session, built.bytes, 4096).ok());
  EXPECT_EQ(session.output.SamplesOf(kVideo).size(), 10U);
}

TEST(MatroskaDemuxerTest, TheSeekMapIsEmptyBeforeAnyClusterIsKnown) {
  const BuiltMkv built = BuildMkv(BasicSpec());
  Session session;
  session.demuxer.SetSourceInfo(built.bytes.size(), true);
  ASSERT_TRUE(
      session.demuxer
          .Append(0, penumbra::SharedBytes::Copy(std::span<const uint8_t>(
                         built.bytes.data(), built.first_cluster_offset)))
          .ok());
  for (int turn = 0; turn < 20; ++turn) {
    const auto step = session.demuxer.Step();
    ASSERT_TRUE(step.ok());
    if (step->kind == StepKind::kNeedMoreData) {
      break;
    }
  }
  ASSERT_NE(session.output.seek_map, nullptr);
  EXPECT_FALSE(session.output.seek_map->IsSeekable());
  EXPECT_FALSE(
      session.output.seek_map->SeekPointFor(penumbra::MediaTime::FromSeconds(1))
          .has_value());
}

TEST(MatroskaDemuxerTest, SeekPreRollMovesTheSeekPointEarlier) {
  MkvFileSpec spec = BasicSpec();
  spec.cues = true;
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 4096).ok());
  const auto point = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(2050));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->time, penumbra::MediaTime::FromMilliseconds(1000));
  const auto exact = session.output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(2080));
  ASSERT_TRUE(exact.has_value());
  EXPECT_EQ(exact->time, penumbra::MediaTime::FromMilliseconds(2000));
}

TEST(MatroskaDemuxerTest, SeekBeforeTheTracksIsAPreconditionFailure) {
  Session session;
  EXPECT_EQ(session.demuxer.Seek(SeekPoint{}).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(MatroskaDemuxerTest, AppendAtTheWrongOffsetFails) {
  Session session;
  EXPECT_EQ(
      session.demuxer.Append(5, penumbra::SharedBytes::Copy(Bytes{1})).code(),
      absl::StatusCode::kFailedPrecondition);
}

TEST(MatroskaDemuxerTest, HeaderStrippingPrependsTheStrippedBytes) {
  MkvFileSpec spec;
  MkvTrackSpec track = VideoTrack();
  track.header_strip = {0xAA, 0xBB};
  spec.tracks = {track};
  MkvClusterSpec cluster;
  cluster.blocks = {VideoBlock(0, 0, true)};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 1U);
  ASSERT_EQ(video[0].data.size(), 102U);
  EXPECT_EQ(video[0].data.span()[0], 0xAA);
  EXPECT_EQ(video[0].data.span()[1], 0xBB);
  EXPECT_EQ(video[0].data.span()[2], FrameBytes(kVideo, 0, 100)[0]);
}

TEST(MatroskaDemuxerTest, SkipsTracksItCannotOffer) {
  MkvFileSpec spec = BasicSpec(1);
  MkvTrackSpec encrypted = VideoTrack(3);
  encrypted.encrypted = true;
  MkvTrackSpec compressed = VideoTrack(4);
  compressed.zlib = true;
  MkvTrackSpec vp8;
  vp8.number = 5;
  vp8.codec = MkvTestCodec::kUnsupported;
  spec.tracks.push_back(encrypted);
  spec.tracks.push_back(compressed);
  spec.tracks.push_back(vp8);
  MkvBlockSpec stray = VideoBlock(0, 0, true);
  stray.track = 3;
  spec.clusters[0].blocks.push_back(stray);
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 17).ok());
  EXPECT_EQ(session.output.streams.size(), 2U);
  EXPECT_EQ(session.demuxer.Stats().skipped_tracks, 3U);
  EXPECT_EQ(session.demuxer.Stats().dropped_blocks, 1U);
  EXPECT_EQ(session.output.samples.size(), 10U);
}

TEST(MatroskaDemuxerTest, OffersVp8AndVorbisTracks) {
  MkvFileSpec spec = BasicSpec(1);
  MkvTrackSpec video;
  video.codec = MkvTestCodec::kVp8;
  MkvTrackSpec audio;
  audio.number = 2;
  audio.codec = MkvTestCodec::kVorbis;
  spec.tracks = {video, audio};
  const MkvBlockSpec video_block = VideoBlock(0, 0, true);
  MkvBlockSpec audio_block;
  audio_block.track = 2;
  audio_block.frames = {Bytes{0x00, 0x01}};
  spec.clusters[0].blocks = {video_block, audio_block};
  Session session;
  const absl::Status status = Feed(session, BuildMkv(spec).bytes, 19);
  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(session.output.streams.size(), 2U);
  EXPECT_EQ(session.output.streams[0].codec, penumbra::CodecId::kVp8);
  EXPECT_EQ(session.output.streams[0].codec_string, "vp8");
  EXPECT_TRUE(session.output.streams[0].codec_config.empty());
  EXPECT_EQ(session.output.streams[1].codec, penumbra::CodecId::kVorbis);
  EXPECT_EQ(session.output.streams[1].codec_string, "vorbis");
  EXPECT_FALSE(session.output.streams[1].codec_config.empty());
  EXPECT_EQ(session.demuxer.Stats().skipped_tracks, 0U);
}

TEST(MatroskaDemuxerTest, OffersTheoraTracksWithXiphHeaders) {
  MkvFileSpec spec = BasicSpec(1);
  MkvTrackSpec video;
  video.codec = MkvTestCodec::kTheora;
  spec.tracks = {video};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  ASSERT_EQ(session.output.streams.size(), 1U);
  EXPECT_EQ(session.output.streams[0].codec, penumbra::CodecId::kTheora);
  EXPECT_EQ(session.output.streams[0].codec_string, "theora");
}

TEST(MatroskaDemuxerTest, AFileWithNoPlayableTrackIsRejected) {
  MkvFileSpec spec;
  MkvTrackSpec vp8;
  vp8.codec = MkvTestCodec::kUnsupported;
  spec.tracks = {vp8};
  MkvClusterSpec cluster;
  cluster.blocks = {VideoBlock(0, 0, true)};
  spec.clusters = {cluster};
  Session session;
  EXPECT_EQ(Feed(session, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kUnimplemented);
}

TEST(MatroskaDemuxerTest, RejectsOtherDocumentTypesAndNewerVersions) {
  MkvFileSpec spec = BasicSpec(1);
  spec.doc_type = "ogg";
  Session other;
  EXPECT_EQ(Feed(other, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kUnimplemented);
  Session garbage;
  const Bytes text = {'n', 'o', 't', ' ', 'm', 'k', 'v', '!', '!', '!'};
  EXPECT_EQ(Feed(garbage, text, kWholeFile).code(),
            absl::StatusCode::kDataLoss);
  Session empty;
  EXPECT_EQ(Feed(empty, Bytes(), kWholeFile).code(),
            absl::StatusCode::kDataLoss);
}

TEST(MatroskaDemuxerTest, AcceptsTheMatroskaDocumentType) {
  MkvFileSpec spec = BasicSpec(1);
  spec.doc_type = "matroska";
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  EXPECT_EQ(session.output.samples.size(), 10U);
}

TEST(MatroskaDemuxerTest, HonoursTheTimestampScale) {
  MkvFileSpec spec = BasicSpec(1);
  spec.timestamp_scale = 500000;
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  EXPECT_EQ(session.output.streams[0].time_base,
            penumbra::Rational::Of(1, 2000));
  const auto video = session.output.SamplesOf(kVideo);
  EXPECT_EQ(video[1].pts.value, 200);
  EXPECT_EQ(video[1].duration.value, 80);
}

TEST(MatroskaDemuxerTest, TracksMayPrecedeTheInfo) {
  MkvFileSpec spec = BasicSpec(1);
  spec.tracks_before_info = true;
  spec.timestamp_scale = 500000;
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 9).ok());
  EXPECT_EQ(session.output.stream_announcements, 1U);
  EXPECT_EQ(session.output.streams[0].time_base,
            penumbra::Rational::Of(1, 2000));
}

TEST(MatroskaDemuxerTest, ANewInitialisationAnnouncesAgainWithAConfigChange) {
  const Bytes first = BuildMkv(BasicSpec(1)).bytes;
  MkvFileSpec changed = BasicSpec(1);
  changed.tracks[0].width = 320;
  const Bytes second = BuildMkv(changed).bytes;
  Bytes joined = first;
  joined.insert(joined.end(), second.begin(), second.end());
  Session session;
  ASSERT_TRUE(Feed(session, joined, 33).ok());
  EXPECT_EQ(session.output.stream_announcements, 2U);
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 10U);
  EXPECT_EQ(video[0].config_change, nullptr);
  ASSERT_NE(video[5].config_change, nullptr);
  const auto* info =
      std::get_if<penumbra::VideoInfo>(&video[5].config_change->format);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->coded_width, 320);
  EXPECT_EQ(video[6].config_change, nullptr);
}

TEST(MatroskaDemuxerTest, ResetAcceptsClustersWithoutAnInitialisationSegment) {
  MkvFileSpec init = BasicSpec(1);
  init.clusters.clear();
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(init).bytes, kWholeFile).ok());
  EXPECT_EQ(session.output.streams.size(), 2U);
  const BuiltMkv full = BuildMkv(BasicSpec(2));
  const Bytes clusters(full.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                full.first_cluster_offset),
                       full.bytes.end());
  session.demuxer.Reset(0);
  ASSERT_TRUE(Feed(session, clusters, 29).ok());
  EXPECT_EQ(session.output.stream_announcements, 1U);
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 10U);
  EXPECT_TRUE(video[0].discontinuity);
  EXPECT_EQ(video[9].pts.value, 1800);
}

TEST(MatroskaDemuxerTest, ABackwardsClusterTimestampIsADiscontinuity) {
  MkvFileSpec spec = BasicSpec(2);
  spec.clusters[0].timestamp = 1000;
  spec.clusters[1].timestamp = 100;
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 10U);
  EXPECT_FALSE(video[1].discontinuity);
  EXPECT_TRUE(video[5].discontinuity);
  EXPECT_FALSE(video[6].discontinuity);
}

TEST(MatroskaDemuxerTest, ABlockBeforeTheClusterTimestampIsCorrupt) {
  MkvFileSpec spec = BasicSpec(1);
  spec.clusters[0].omit_timestamp = true;
  Session session;
  EXPECT_EQ(Feed(session, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kDataLoss);
}

TEST(MatroskaDemuxerTest, ClustersBeforeTheTracksAreCorrupt) {
  MkvFileSpec spec = BasicSpec(1);
  spec.omit_tracks = true;
  Session session;
  EXPECT_EQ(Feed(session, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kDataLoss);
}

TEST(MatroskaDemuxerTest, JunkBeforeTheFirstClusterIsSkipped) {
  MkvFileSpec spec = BasicSpec(2);
  spec.unknown_segment_size = true;
  spec.junk_before_clusters = Bytes(50, 0x0A);
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 37).ok());
  EXPECT_EQ(session.output.samples.size(), 20U);
  EXPECT_EQ(session.demuxer.Stats().resyncs, 1U);
}

TEST(MatroskaDemuxerTest, AChildLargerThanItsClusterResynchronisesAtTheNext) {
  MkvFileSpec spec = BasicSpec(3);
  spec.clusters[1].trailing_raw = Bytes{0xEA, 0x4F, 0xFF, 0xFF, 1, 2, 3};
  const BuiltMkv built = BuildMkv(spec);
  Session session;
  ASSERT_TRUE(Feed(session, built.bytes, 1U << 20).ok());
  EXPECT_EQ(session.demuxer.Stats().resyncs, 1U);
  EXPECT_EQ(session.output.SamplesOf(kVideo).size(), 15U);
  const auto video = session.output.SamplesOf(kVideo);
  EXPECT_EQ(video.back().pts.value, 2800);
}

TEST(MatroskaDemuxerTest, UnreadableDataWithoutAnyLaterElementEndsTheStream) {
  MkvFileSpec spec = BasicSpec(1);
  spec.unknown_segment_size = true;
  Bytes bytes = BuildMkv(spec).bytes;
  const Bytes junk(300, 0x0A);
  bytes.insert(bytes.end(), junk.begin(), junk.end());
  Session session;
  ASSERT_TRUE(Feed(session, bytes, 41).ok());
  EXPECT_EQ(session.output.samples.size(), 10U);
}

TEST(MatroskaDemuxerTest, EveryTruncationYieldsSamplesOrDataLoss) {
  MkvFileSpec spec = BasicSpec(2);
  spec.cues = true;
  spec.cues_at_end = true;
  const Bytes bytes = BuildMkv(spec).bytes;
  for (size_t length = 0; length < bytes.size(); ++length) {
    Session session;
    const Bytes prefix(bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const absl::Status status = Feed(session, prefix, 61);
    EXPECT_TRUE(status.ok() || status.code() == absl::StatusCode::kDataLoss)
        << length << " " << status;
  }
}

TEST(MatroskaDemuxerTest, EveryCorruptedByteYieldsSamplesOrAnError) {
  MkvFileSpec spec = BasicSpec(1);
  spec.cues = true;
  const Bytes original = BuildMkv(spec).bytes;
  for (size_t position = 0; position < original.size(); ++position) {
    for (const uint8_t value : {uint8_t{0x00}, uint8_t{0xFF}}) {
      Bytes bytes = original;
      bytes[position] = value;
      Session session;
      const absl::Status status = Feed(session, bytes, 1U << 20);
      EXPECT_TRUE(status.ok() || status.code() == absl::StatusCode::kDataLoss ||
                  status.code() == absl::StatusCode::kUnimplemented)
          << position << " " << status;
    }
  }
}

TEST(MatroskaDemuxerTest, LongClustersYieldWithinTheStepBudget) {
  MkvFileSpec spec;
  spec.tracks = {VideoTrack()};
  MkvClusterSpec cluster;
  for (size_t index = 0; index < 600; ++index) {
    cluster.blocks.push_back(
        VideoBlock(index, static_cast<int16_t>(index % 1000), index == 0));
  }
  spec.clusters = {cluster};
  const Bytes bytes = BuildMkv(spec).bytes;
  Session session;
  session.demuxer.SetSourceInfo(bytes.size(), true);
  ASSERT_TRUE(
      session.demuxer.Append(0, penumbra::SharedBytes::Copy(bytes)).ok());
  size_t yields = 0;
  for (size_t guard = 0; guard < 1000; ++guard) {
    const auto step = session.demuxer.Step();
    ASSERT_TRUE(step.ok());
    if (step->kind == StepKind::kYield) {
      ++yields;
    } else if (step->kind == StepKind::kNeedMoreData) {
      session.demuxer.EndOfInput();
    } else if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  EXPECT_GE(yields, 4U);
  EXPECT_EQ(session.output.samples.size(), 600U);
}

TEST(MatroskaDemuxerTest, FrameDataIsASliceOfTheAppendedChunk) {
  const Bytes bytes = BuildMkv(BasicSpec(1)).bytes;
  const penumbra::SharedBytes chunk = penumbra::SharedBytes::Copy(bytes);
  Session session;
  session.demuxer.SetSourceInfo(bytes.size(), true);
  ASSERT_TRUE(session.demuxer.Append(0, chunk).ok());
  for (size_t guard = 0; guard < 1000; ++guard) {
    const auto step = session.demuxer.Step();
    ASSERT_TRUE(step.ok());
    if (step->kind == StepKind::kNeedMoreData) {
      session.demuxer.EndOfInput();
    } else if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  const auto video = session.output.SamplesOf(kVideo);
  ASSERT_EQ(video.size(), 5U);
  const auto whole_begin =
      reinterpret_cast<std::uintptr_t>(chunk.span().data());
  const std::uintptr_t whole_end = whole_begin + chunk.size();
  for (const penumbra::EncodedSample& sample : video) {
    const auto begin =
        reinterpret_cast<std::uintptr_t>(sample.data.span().data());
    EXPECT_GE(begin, whole_begin);
    EXPECT_LE(begin + sample.data.size(), whole_end);
  }
}

class CodecTest : public ::testing::TestWithParam<MkvTestCodec> {};

TEST_P(CodecTest, EveryCodecIsDescribed) {
  MkvFileSpec spec;
  MkvTrackSpec track;
  track.number = 1;
  track.codec = GetParam();
  track.default_duration_ns = kTwentyMilliseconds;
  spec.tracks = {track};
  MkvBlockSpec block;
  block.track = 1;
  block.frames = {FrameBytes(1, 0, 50)};
  MkvClusterSpec cluster;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, 19).ok());
  ASSERT_EQ(session.output.streams.size(), 1U);
  const penumbra::StreamInfo& stream = session.output.streams[0];
  EXPECT_NE(stream.codec, penumbra::CodecId::kUnknown);
  EXPECT_FALSE(stream.codec_string.empty());
  if (GetParam() == MkvTestCodec::kAac) {
    EXPECT_EQ(stream.aac_framing, penumbra::AacFraming::kRaw);
  }
  EXPECT_EQ(session.output.samples.size(), 1U);
  EXPECT_EQ(session.output.samples[0].duration.value, 20);
}

INSTANTIATE_TEST_SUITE_P(
    Codecs, CodecTest,
    ::testing::Values(MkvTestCodec::kVp9, MkvTestCodec::kAv1,
                      MkvTestCodec::kAac, MkvTestCodec::kFlac,
                      MkvTestCodec::kAc3, MkvTestCodec::kMp3));

TEST(MatroskaDemuxerTest, CodecPrivateIsCarriedAsTheContainerStoresIt) {
  MkvFileSpec spec;
  MkvTrackSpec av1;
  av1.number = 1;
  av1.codec = MkvTestCodec::kAv1;
  MkvTrackSpec mp3;
  mp3.number = 2;
  mp3.codec = MkvTestCodec::kMp3;
  MkvTrackSpec flac;
  flac.number = 3;
  flac.codec = MkvTestCodec::kFlac;
  spec.tracks = {av1, OpusTrack(4), mp3, flac};
  MkvBlockSpec block;
  block.track = 1;
  block.frames = {FrameBytes(1, 0, 50)};
  MkvClusterSpec cluster;
  cluster.blocks = {block};
  spec.clusters = {cluster};
  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());
  ASSERT_EQ(session.output.streams.size(), 4U);
  EXPECT_EQ(session.output.streams[0].codec_string.rfind("av01.", 0), 0U);
  EXPECT_EQ(session.output.streams[0].codec_config.size(), 17U);
  EXPECT_EQ(session.output.streams[1].codec_config.span()[0], 'O');
  EXPECT_EQ(session.output.streams[2].codec, penumbra::CodecId::kMp3);
  EXPECT_EQ(session.output.streams[2].codec_string, "mp4a.6B");
  EXPECT_TRUE(session.output.streams[2].codec_config.empty());
  EXPECT_EQ(session.output.streams[3].codec, penumbra::CodecId::kFlac);
  EXPECT_EQ(session.output.streams[3].codec_config.span()[0], 'f');
}

TEST(MatroskaDemuxerTest, ADamagedCodecPrivateRejectsTheFile) {
  MkvFileSpec spec = BasicSpec(1);
  spec.tracks[1].channels = 0;
  Session session;
  EXPECT_EQ(Feed(session, BuildMkv(spec).bytes, kWholeFile).code(),
            absl::StatusCode::kDataLoss);
}

TEST(MatroskaDemuxerTest, EmitsAssSubtitleTrackAndPreservesItsHeaderAndEvent) {
  constexpr std::string_view header =
      "[Script Info]\nPlayResX: 640\nPlayResY: 360\n[Events]\n"
      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
      "Effect, Text\n";
  MkvFileSpec spec;
  MkvTrackSpec subtitle;
  subtitle.number = 3;
  subtitle.codec = MkvTestCodec::kAss;
  subtitle.codec_private.assign(header.begin(), header.end());
  spec.tracks = {VideoTrack(), subtitle};
  MkvBlockSpec block;
  block.track = 3;
  block.group = true;
  block.duration = 2000;
  const std::string event = "0,Default,,0,0,0,,Hello";
  block.frames.emplace_back(event.begin(), event.end());
  MkvClusterSpec cluster;
  cluster.blocks = {VideoBlock(0, 0, true), block};
  spec.clusters = {cluster};

  Session session;
  ASSERT_TRUE(Feed(session, BuildMkv(spec).bytes, kWholeFile).ok());

  ASSERT_EQ(session.output.streams.size(), 2U);
  EXPECT_EQ(session.output.streams[1].type(), penumbra::StreamType::kText);
  EXPECT_EQ(session.output.streams[1].codec_string, "ass");
  EXPECT_EQ(session.output.streams[1].codec_config.span().size(),
            header.size());
  ASSERT_EQ(session.output.samples.size(), 2U);
  EXPECT_EQ(session.output.samples[1].track_id, 3U);
  EXPECT_EQ(session.output.samples[1].data.size(), event.size());
  EXPECT_EQ(session.output.samples[1].duration.value,
            std::optional<int64_t>(2000));
}

}
}
