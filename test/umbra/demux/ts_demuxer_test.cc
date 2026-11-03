#include "umbra/demux/ts_demuxer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "ts_builder.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_framing.h"

namespace umbra::demux::test {
namespace {

constexpr uint16_t kVideoPid = 0x101;
constexpr uint16_t kAudioPid = 0x102;

struct Playback {
  RecordingOutput output;
  TsDemuxerStats stats;
  absl::Status status;

  void Play(const Bytes& data, size_t chunk = size_t{1} << 20,
            TsDemuxerOptions options = {}) {
    TsDemuxer demuxer(output, options);
    status = DriveBytes(demuxer, data, chunk).status;
    stats = demuxer.Stats();
  }
};

std::string RenderSample(const penumbra::EncodedSample& sample) {
  return absl::StrCat(sample.track_id, " ", sample.pts.value, " ",
                      sample.dts.value, " ", sample.duration.value, " ",
                      sample.data.size(), sample.key_frame ? " K" : " _",
                      sample.discontinuity ? " D" : " _",
                      sample.config_change != nullptr ? " C" : " _", "\n");
}

std::string RenderAll(const RecordingOutput& output) {
  std::string text;
  for (const penumbra::StreamInfo& stream : output.streams) {
    absl::StrAppend(&text, "stream ", stream.track_id, " ", stream.codec_string,
                    "\n");
  }
  for (const penumbra::EncodedSample& sample : output.samples) {
    absl::StrAppend(&text, RenderSample(sample));
  }
  return text;
}

size_t CountDiscontinuities(const std::vector<penumbra::EncodedSample>& list) {
  return static_cast<size_t>(
      std::ranges::count_if(list, [](const penumbra::EncodedSample& sample) {
        return sample.discontinuity;
      }));
}

uint16_t PidOf(const Bytes& packet) {
  return static_cast<uint16_t>(((packet[1] & 0x1FU) << 8U) | packet[2]);
}

bool StartsPayload(const Bytes& packet) { return (packet[1] & 0x40U) != 0; }

size_t FindContinuation(const Synthetic& synthetic, size_t after,
                        uint16_t pid) {
  for (size_t index = after; index < synthetic.packets.size(); ++index) {
    if (PidOf(synthetic.packets[index]) == pid &&
        !StartsPayload(synthetic.packets[index])) {
      return index;
    }
  }
  return 0;
}

TEST(TsDemuxerTest, DemuxesVideoAndAudioWithExactTimestamps) {
  const Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.stream_announcements, 1U);
  ASSERT_EQ(run.output.streams.size(), 2U);
  const penumbra::StreamInfo& video = run.output.streams[0];
  EXPECT_EQ(video.track_id, kVideoPid);
  EXPECT_EQ(video.codec, penumbra::CodecId::kH264);
  EXPECT_EQ(video.nal_unit_framing, penumbra::NalUnitFraming::kAnnexB);
  EXPECT_EQ(video.nal_length_size, 4U);
  EXPECT_EQ(video.codec_string, "avc1.4d400b");
  EXPECT_EQ(video.time_base, ts::kTimeBase);
  EXPECT_EQ(std::get<penumbra::VideoInfo>(video.format).display_width, 160);
  EXPECT_EQ(std::get<penumbra::VideoInfo>(video.format).display_height, 90);
  const penumbra::StreamInfo& audio = run.output.streams[1];
  EXPECT_EQ(audio.codec, penumbra::CodecId::kAac);
  EXPECT_EQ(audio.aac_framing, penumbra::AacFraming::kAdts);
  EXPECT_EQ(audio.codec_string, "mp4a.40.2");
  EXPECT_EQ(std::get<penumbra::AudioInfo>(audio.format).sample_rate, 48000);
  EXPECT_EQ(std::get<penumbra::AudioInfo>(audio.format).channel_count, 2);
  EXPECT_EQ(audio.codec_config.size(), 2U);

  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), 50U);
  for (size_t index = 0; index < frames.size(); ++index) {
    const int64_t dts = 900000 + (static_cast<int64_t>(index) * kFrameTicks);
    EXPECT_EQ(frames[index].dts.value, dts);
    EXPECT_EQ(frames[index].pts.value, dts + kVideoDelayTicks);
    EXPECT_EQ(frames[index].duration.value, kFrameTicks);
    EXPECT_EQ(frames[index].key_frame, index % 25 == 0);
    EXPECT_FALSE(frames[index].discontinuity);
    EXPECT_EQ(frames[index].config_change, nullptr);
  }
  const auto sound = run.output.SamplesOf(kAudioPid);
  ASSERT_GE(sound.size(), 30U);
  for (size_t index = 0; index < sound.size(); ++index) {
    EXPECT_EQ(sound[index].pts.value,
              900000 + (static_cast<int64_t>(index) * 1920));
    EXPECT_EQ(sound[index].duration.value, 1920);
    EXPECT_TRUE(sound[index].key_frame);
  }
  EXPECT_EQ(run.stats.pcr_count, 2U);
  EXPECT_EQ(run.output.seek_map_count, 1U);
  ASSERT_NE(run.output.seek_map, nullptr);
  EXPECT_TRUE(run.output.seek_map->IsSeekable());
  EXPECT_EQ(run.output.seek_map->Duration().InMicroseconds(), 2040000);
}

TEST(TsDemuxerTest, FeedingOneByteAtATimeGivesTheSameOutput) {
  SyntheticSpec spec;
  spec.captions = true;
  spec.id3 = true;
  spec.frames = 30;
  const Bytes data = Flatten(BuildSynthetic(spec).packets);
  Playback whole;
  whole.Play(data);
  ASSERT_TRUE(whole.status.ok());
  for (const size_t chunk : {size_t{1}, size_t{7}, size_t{188}, size_t{1000}}) {
    Playback run;
    run.Play(data, chunk);
    ASSERT_TRUE(run.status.ok()) << chunk;
    EXPECT_EQ(RenderAll(run.output), RenderAll(whole.output)) << chunk;
    EXPECT_EQ(run.output.items.size(), whole.output.items.size());
  }
}

TEST(TsDemuxerTest, BoundedAndUnboundedVideoPesGiveTheSameSamples) {
  SyntheticSpec spec;
  spec.frames = 20;
  Playback unbounded;
  unbounded.Play(Flatten(BuildSynthetic(spec).packets));
  spec.bounded_video = true;
  Playback bounded;
  bounded.Play(Flatten(BuildSynthetic(spec).packets));
  ASSERT_TRUE(unbounded.status.ok());
  ASSERT_TRUE(bounded.status.ok());
  for (const uint16_t pid : {kVideoPid, kAudioPid}) {
    std::string bounded_text;
    std::string unbounded_text;
    for (const auto& sample : bounded.output.SamplesOf(pid)) {
      bounded_text += RenderSample(sample);
    }
    for (const auto& sample : unbounded.output.SamplesOf(pid)) {
      unbounded_text += RenderSample(sample);
    }
    EXPECT_EQ(bounded_text, unbounded_text) << pid;
  }
  EXPECT_EQ(unbounded.output.SamplesOf(kVideoPid).size(), 20U);
}

TEST(TsDemuxerTest, TimestampsCrossingThe33BitWrapStayContinuous) {
  SyntheticSpec spec;
  spec.start = (uint64_t{1} << 33) - (uint64_t{20} * 3600);
  spec.frames = 60;
  Playback run;
  run.Play(Flatten(BuildSynthetic(spec).packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), 60U);
  for (size_t index = 0; index < frames.size(); ++index) {
    const int64_t dts = static_cast<int64_t>(spec.start) +
                        (static_cast<int64_t>(index) * kFrameTicks);
    EXPECT_EQ(frames[index].dts.value, dts) << index;
    EXPECT_EQ(frames[index].pts.value, dts + kVideoDelayTicks) << index;
    EXPECT_FALSE(frames[index].discontinuity) << index;
  }
  EXPECT_GT(frames.back().dts.value, int64_t{1} << 33);
  const auto sound = run.output.SamplesOf(kAudioPid);
  ASSERT_GT(sound.size(), 40U);
  for (size_t index = 1; index < sound.size(); ++index) {
    EXPECT_EQ(sound[index].pts.value - sound[index - 1].pts.value, 1920);
    EXPECT_FALSE(sound[index].discontinuity);
  }
  EXPECT_GT(sound.back().pts.value, int64_t{1} << 33);
}

TEST(TsDemuxerTest, ALostPacketDropsTheFrameAndFlagsTheNextSample) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  const size_t victim =
      FindContinuation(synthetic, synthetic.psi_positions[2], kVideoPid);
  ASSERT_NE(victim, 0U);
  synthetic.packets.erase(synthetic.packets.begin() +
                          static_cast<ptrdiff_t>(victim));
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.continuity_errors, 1U);
  const auto frames = run.output.SamplesOf(kVideoPid);
  EXPECT_EQ(frames.size(), 49U);
  EXPECT_EQ(CountDiscontinuities(frames), 1U);
  for (size_t index = 1; index < frames.size(); ++index) {
    EXPECT_GT(frames[index].dts.value, frames[index - 1].dts.value);
  }
  EXPECT_EQ(CountDiscontinuities(run.output.SamplesOf(kAudioPid)), 0U);
}

TEST(TsDemuxerTest, ADuplicatedPacketIsIgnored) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  Playback baseline;
  baseline.Play(Flatten(synthetic.packets));
  const size_t index =
      FindContinuation(synthetic, synthetic.psi_positions[1], kVideoPid);
  synthetic.packets.insert(
      synthetic.packets.begin() + static_cast<ptrdiff_t>(index),
      synthetic.packets[index]);
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok());
  EXPECT_EQ(run.stats.duplicate_packets, 1U);
  EXPECT_EQ(RenderAll(run.output), RenderAll(baseline.output));
}

TEST(TsDemuxerTest, TheDiscontinuityIndicatorSuppressesTheContinuityError) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x1B, kVideoPid, {}}}));
  for (int index = 0; index < 4; ++index) {
    PesOptions options;
    options.dts = 90000 + (static_cast<uint64_t>(index) * 3600);
    options.pts = options.dts;
    options.discontinuity = index == 2;
    add(writer.Pes(kVideoPid, H264AccessUnit(index == 0, 50), options));
  }
  packets.erase(packets.begin() + 3);
  Playback run;
  run.Play(Flatten(packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.continuity_errors, 0U);
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), 3U);
  EXPECT_FALSE(frames[0].discontinuity);
  EXPECT_TRUE(frames[1].discontinuity);
  EXPECT_FALSE(frames[2].discontinuity);
}

TEST(TsDemuxerTest, LostSyncIsRecoveredFromAndFlagged) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  Bytes data = Flatten(synthetic.packets);
  const size_t cut = (synthetic.psi_positions[3] * 188) + (size_t{5} * 188);
  data.insert(data.begin() + static_cast<ptrdiff_t>(cut), 101, 0xAA);
  Playback run;
  run.Play(data);
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.sync_losses, 1U);
  const auto frames = run.output.SamplesOf(kVideoPid);
  EXPECT_GE(frames.size(), 48U);
  EXPECT_EQ(CountDiscontinuities(frames), 1U);
  EXPECT_EQ(CountDiscontinuities(run.output.SamplesOf(kAudioPid)), 1U);
}

TEST(TsDemuxerTest, LeadingGarbageAndATruncatedTailAreTolerated) {
  const Bytes clean = Flatten(BuildSynthetic(SyntheticSpec{}).packets);
  Playback baseline;
  baseline.Play(clean);
  Bytes data(333, 0x00);
  data.insert(data.end(), clean.begin(), clean.end());
  data.resize(data.size() - 90);
  Playback run;
  run.Play(data);
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.sync_losses, 0U);
  EXPECT_EQ(run.output.SamplesOf(kVideoPid).size(),
            baseline.output.SamplesOf(kVideoPid).size());
}

TEST(TsDemuxerTest, APmtWithABadCrcFlagsADiscontinuityAndPlaybackContinues) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  Bytes& pmt = synthetic.packets[synthetic.psi_positions[2] + 1];
  pmt[12] ^= 0x01;
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.crc_errors, 1U);
  EXPECT_EQ(run.output.stream_announcements, 1U);
  EXPECT_EQ(run.output.SamplesOf(kVideoPid).size(), 50U);
  EXPECT_EQ(CountDiscontinuities(run.output.SamplesOf(kVideoPid)), 1U);
  EXPECT_EQ(CountDiscontinuities(run.output.SamplesOf(kAudioPid)), 1U);
}

TEST(TsDemuxerTest, ABadFirstPmtIsReplacedByTheNextOne) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  synthetic.packets[synthetic.psi_positions[0] + 1][12] ^= 0x01;
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.crc_errors, 1U);
  ASSERT_EQ(run.output.stream_announcements, 1U);
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_FALSE(frames.empty());
  EXPECT_TRUE(frames.front().key_frame);
}

TEST(TsDemuxerTest, TransportErrorsAndScrambledPacketsAreCountedAndSkipped) {
  Synthetic synthetic = BuildSynthetic(SyntheticSpec{});
  const size_t first =
      FindContinuation(synthetic, synthetic.psi_positions[1], kVideoPid);
  synthetic.packets[first][1] |= 0x80;
  const size_t second =
      FindContinuation(synthetic, synthetic.psi_positions[3], kVideoPid);
  synthetic.packets[second][3] |= 0xC0;
  Playback run;
  run.Play(Flatten(synthetic.packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.transport_errors, 1U);
  EXPECT_EQ(run.stats.scrambled_packets, 1U);
  EXPECT_LT(run.output.SamplesOf(kVideoPid).size(), 50U);
  EXPECT_GE(CountDiscontinuities(run.output.SamplesOf(kVideoPid)), 1U);
}

TEST(TsDemuxerTest, APmtChangeAnnouncesTheStreamsAgainWithConfigChanges) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x1B, kVideoPid, {}}}, 0));
  for (int index = 0; index < 6; ++index) {
    PesOptions options;
    options.dts = 90000 + (static_cast<uint64_t>(index) * 3600);
    options.pts = options.dts;
    add(writer.Pes(kVideoPid, H264AccessUnit(index == 0, 100), options));
  }
  add(writer.Pmt(0x100, 1, kVideoPid,
                 {{0x1B, kVideoPid, {}}, {0x0F, kAudioPid, {}}}, 1));
  for (int index = 6; index < 12; ++index) {
    PesOptions options;
    options.dts = 90000 + (static_cast<uint64_t>(index) * 3600);
    options.pts = options.dts;
    add(writer.Pes(kVideoPid, H264AccessUnit(index == 6, 100), options));
    PesOptions audio;
    audio.pts = options.dts;
    audio.stream_id = 0xC0;
    audio.bounded = true;
    add(writer.Pes(kAudioPid, AdtsFrames(2, 80), audio));
  }
  Playback run;
  run.Play(Flatten(packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(run.stats.pmt_changes, 1U);
  ASSERT_EQ(run.output.stream_announcements, 2U);
  ASSERT_EQ(run.output.streams.size(), 2U);
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), 12U);
  EXPECT_NE(frames[6].config_change, nullptr);
  EXPECT_TRUE(frames[6].discontinuity);
  EXPECT_EQ(frames[5].config_change, nullptr);
  EXPECT_EQ(run.output.SamplesOf(kAudioPid).size(), 12U);
}

TEST(TsDemuxerTest, AChangedParameterSetPutsAConfigChangeOnTheSample) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x1B, kVideoPid, {}}}));
  for (int index = 0; index < 6; ++index) {
    PesOptions options;
    options.dts = 90000 + (static_cast<uint64_t>(index) * 3600);
    options.pts = options.dts;
    const bool key = index == 0 || index == 3 || index == 5;
    add(writer.Pes(kVideoPid, H264AccessUnit(key, 100, {}, index >= 3),
                   options));
  }
  Playback run;
  run.Play(Flatten(packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), 6U);
  EXPECT_EQ(frames[0].config_change, nullptr);
  ASSERT_NE(frames[3].config_change, nullptr);
  EXPECT_EQ(std::get<penumbra::VideoInfo>(frames[3].config_change->format)
                .coded_width,
            320);
  EXPECT_EQ(frames[4].config_change, nullptr);
  EXPECT_EQ(frames[5].config_change, nullptr);
  EXPECT_EQ(
      std::get<penumbra::VideoInfo>(run.output.streams[0].format).coded_width,
      160);
}

Bytes TwoProgramStream() {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}, {2, 0x200}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x1B, kVideoPid, {}}}));
  add(writer.Pmt(0x200, 2, 0x201, {{0x0F, 0x201, {}}}));
  for (int index = 0; index < 5; ++index) {
    PesOptions video;
    video.dts = 90000 + (static_cast<uint64_t>(index) * 3600);
    video.pts = video.dts;
    add(writer.Pes(kVideoPid, H264AccessUnit(index == 0, 100), video));
    PesOptions audio;
    audio.pts = video.dts;
    audio.stream_id = 0xC0;
    audio.bounded = true;
    add(writer.Pes(0x201, AdtsFrames(1, 80), audio));
  }
  return Flatten(packets);
}

TEST(TsDemuxerTest, TheFirstProgramIsPlayedByDefault) {
  Playback run;
  run.Play(TwoProgramStream());
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.streams.size(), 1U);
  EXPECT_EQ(run.output.streams[0].track_id, kVideoPid);
  EXPECT_EQ(run.output.samples.size(), 5U);
}

TEST(TsDemuxerTest, TheProgramNumberOptionSelectsAnotherProgram) {
  Playback run;
  TsDemuxerOptions options;
  options.program_number = 2;
  run.Play(TwoProgramStream(), size_t{1} << 20, options);
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.streams.size(), 1U);
  EXPECT_EQ(run.output.streams[0].track_id, 0x201);
  EXPECT_EQ(run.output.streams[0].codec, penumbra::CodecId::kAac);
  EXPECT_EQ(run.output.samples.size(), 5U);
}

TEST(TsDemuxerTest, AMissingProgramIsReportedAsCorruptInput) {
  Playback run;
  TsDemuxerOptions options;
  options.program_number = 9;
  run.Play(TwoProgramStream(), size_t{1} << 20, options);
  EXPECT_EQ(run.status.code(), absl::StatusCode::kDataLoss);
  EXPECT_TRUE(run.output.samples.empty());
}

TEST(TsDemuxerTest, CaptionsInSeiAreEmittedAsATimedStream) {
  SyntheticSpec spec;
  spec.captions = true;
  Playback run;
  run.Play(Flatten(BuildSynthetic(spec).packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.metadata_streams.size(), 1U);
  const MetadataStreamInfo& stream = run.output.metadata_streams[0];
  EXPECT_EQ(stream.kind, MetadataKind::kCaptions);
  EXPECT_EQ(stream.source_track_id, kVideoPid);
  ASSERT_EQ(run.output.items.size(), 10U);
  const TimedMetadataItem& item = run.output.items[1];
  EXPECT_EQ(item.stream_id, stream.stream_id);
  EXPECT_EQ(item.time.value, 900000 + (5 * kFrameTicks) + kVideoDelayTicks);
  const std::span<const uint8_t> payload = item.payload.span();
  EXPECT_EQ(Bytes(payload.begin(), payload.end()),
            (Bytes{0xFC, 0x80, 0x80, 0xFD, 0x81, 0x82}));
  const auto frames = run.output.SamplesOf(kVideoPid);
  const std::array<uint8_t, 4> marker = {'G', 'A', '9', '4'};
  EXPECT_FALSE(std::ranges::search(frames[0].data.span(), marker).empty());
}

TEST(TsDemuxerTest, CaptionExtractionCanBeSwitchedOff) {
  SyntheticSpec spec;
  spec.captions = true;
  Playback run;
  TsDemuxerOptions options;
  options.extract_captions = false;
  run.Play(Flatten(BuildSynthetic(spec).packets), size_t{1} << 20, options);
  ASSERT_TRUE(run.status.ok());
  EXPECT_TRUE(run.output.items.empty());
  EXPECT_TRUE(run.output.metadata_streams.empty());
}

TEST(TsDemuxerTest, Id3TimedMetadataPassesThroughAsAMetadataStream) {
  SyntheticSpec spec;
  spec.id3 = true;
  Playback run;
  run.Play(Flatten(BuildSynthetic(spec).packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.metadata_streams.size(), 1U);
  EXPECT_EQ(run.output.metadata_streams[0].kind, MetadataKind::kId3);
  EXPECT_EQ(run.output.metadata_streams[0].time_base, ts::kTimeBase);
  ASSERT_EQ(run.output.items.size(), 2U);
  EXPECT_EQ(run.output.items[0].time.value, 900000);
  EXPECT_EQ(run.output.items[1].time.value, 900000 + (25 * kFrameTicks));
  const std::span<const uint8_t> payload = run.output.items[0].payload.span();
  ASSERT_GE(payload.size(), 3U);
  EXPECT_EQ(payload[0], 'I');
  EXPECT_EQ(payload[1], 'D');
  EXPECT_EQ(payload[2], '3');
  EXPECT_EQ(run.output.SamplesOf(kVideoPid).size(), 50U);
}

class Ac3Carriage : public ::testing::TestWithParam<EsSpec> {};

TEST_P(Ac3Carriage, FramesAreSplitAndTimedBySampleCount) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  const EsSpec& spec = GetParam();
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, spec.pid, {spec}));
  for (int index = 0; index < 3; ++index) {
    PesOptions options;
    options.pts = 180000 + (static_cast<uint64_t>(index) * 4 * 2880);
    options.stream_id = 0xBD;
    options.bounded = true;
    add(writer.Pes(spec.pid, Ac3Frames(4), options));
  }
  Playback run;
  run.Play(Flatten(packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  ASSERT_EQ(run.output.streams.size(), 1U);
  EXPECT_EQ(run.output.streams[0].codec, penumbra::CodecId::kAc3);
  EXPECT_EQ(run.output.streams[0].codec_string, "ac-3");
  EXPECT_EQ(run.output.streams[0].codec_config.size(), 3U);
  const auto& audio =
      std::get<penumbra::AudioInfo>(run.output.streams[0].format);
  EXPECT_EQ(audio.sample_rate, 48000);
  EXPECT_EQ(audio.channel_count, 6);
  ASSERT_EQ(run.output.samples.size(), 12U);
  for (size_t index = 0; index < 12; ++index) {
    EXPECT_EQ(run.output.samples[index].pts.value,
              180000 + (static_cast<int64_t>(index) * 2880));
    EXPECT_EQ(run.output.samples[index].duration.value, 2880);
    EXPECT_EQ(run.output.samples[index].data.size(), 320U);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Carriages, Ac3Carriage,
    ::testing::Values(EsSpec{0x81, 0x110, {}},
                      EsSpec{0x06, 0x111, {0x6A, 0x01, 0x00}},
                      EsSpec{0x06, 0x112, {0x05, 0x04, 'A', 'C', '-', '3'}}));

TEST(TsDemuxerTest, ATimestampJumpBeyondTheThresholdIsFlagged) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x1B, kVideoPid, {}}}));
  constexpr uint64_t kBase = 1000000;
  constexpr uint64_t kLeap = uint64_t{100} * 90000;
  const std::vector<uint64_t> times = {
      kBase,
      kBase + 3600,
      kBase + 7200,
      kBase + 7200 + kLeap,
      kBase + 7200 + kLeap + 3600,
      kBase + 7200 + kLeap + 3600 - (kLeap / 2)};
  for (size_t index = 0; index < times.size(); ++index) {
    PesOptions options;
    options.dts = times[index];
    options.pts = times[index];
    add(writer.Pes(kVideoPid, H264AccessUnit(index == 0, 100), options));
  }
  Playback run;
  run.Play(Flatten(packets));
  ASSERT_TRUE(run.status.ok()) << run.status;
  const auto frames = run.output.SamplesOf(kVideoPid);
  ASSERT_EQ(frames.size(), times.size());
  EXPECT_FALSE(frames[1].discontinuity);
  EXPECT_TRUE(frames[3].discontinuity);
  EXPECT_FALSE(frames[4].discontinuity);
  EXPECT_TRUE(frames[5].discontinuity);
  EXPECT_EQ(frames[3].dts.value, static_cast<int64_t>(times[3]));
  EXPECT_EQ(frames[2].duration.value, 3600);
}

TEST(TsDemuxerTest, SegmentsFedAfterResetAreFlaggedAndNotAnnouncedAgain) {
  SyntheticSpec first;
  first.frames = 10;
  SyntheticSpec second;
  second.frames = 10;
  second.start = 5000000;
  const Bytes a = Flatten(BuildSynthetic(first).packets);
  const Bytes b = Flatten(BuildSynthetic(second).packets);
  RecordingOutput output;
  TsDemuxerOptions options;
  options.probe_duration = false;
  TsDemuxer demuxer(output, options);
  ASSERT_TRUE(DriveBytes(demuxer, a, 4096).status.ok());
  const size_t first_count = output.samples.size();
  demuxer.Reset(0);
  ASSERT_TRUE(DriveBytes(demuxer, b, 4096).status.ok());
  EXPECT_EQ(output.stream_announcements, 1U);
  ASSERT_GT(output.samples.size(), first_count);
  EXPECT_FALSE(output.samples.front().discontinuity);
  size_t flagged_video = 0;
  size_t flagged_audio = 0;
  for (size_t index = first_count; index < output.samples.size(); ++index) {
    if (output.samples[index].discontinuity) {
      (output.samples[index].track_id == kVideoPid ? flagged_video
                                                   : flagged_audio)++;
    }
  }
  EXPECT_EQ(flagged_video, 1U);
  EXPECT_EQ(flagged_audio, 1U);
  EXPECT_EQ(output.seek_map_count, 1U);
  EXPECT_FALSE(output.seek_map->IsSeekable());
}

TEST(TsDemuxerTest, InputsWithoutTransportPacketsAreCorrupt) {
  Playback empty;
  empty.Play(Bytes{});
  EXPECT_EQ(empty.status.code(), absl::StatusCode::kDataLoss);
  Playback garbage;
  garbage.Play(Bytes(5000, 0x12));
  EXPECT_EQ(garbage.status.code(), absl::StatusCode::kDataLoss);
  Playback short_input;
  short_input.Play(Bytes{0x47, 0x00, 0x00});
  EXPECT_EQ(short_input.status.code(), absl::StatusCode::kDataLoss);
}

TEST(TsDemuxerTest, AStreamWithoutProgramTablesIsCorrupt) {
  TsWriter writer;
  PesOptions options;
  options.pts = 90000;
  options.dts = 90000;
  const Bytes data =
      Flatten(writer.Pes(kVideoPid, H264AccessUnit(true, 100), options));
  Playback run;
  run.Play(data);
  EXPECT_EQ(run.status.code(), absl::StatusCode::kDataLoss);
}

TEST(TsDemuxerTest, AProgramWithNoSupportedStreamIsUnimplemented) {
  TsWriter writer;
  std::vector<Bytes> packets;
  const auto add = [&packets](const std::vector<Bytes>& more) {
    packets.insert(packets.end(), more.begin(), more.end());
  };
  add(writer.Pat({{1, 0x100}}));
  add(writer.Pmt(0x100, 1, kVideoPid, {{0x02, kVideoPid, {}}}));
  for (int index = 0; index < 3; ++index) {
    PesOptions options;
    options.pts = 90000;
    add(writer.Pes(kVideoPid, Bytes(100, 1), options));
  }
  Playback run;
  run.Play(Flatten(packets));
  EXPECT_EQ(run.status.code(), absl::StatusCode::kUnimplemented);
}

TEST(TsDemuxerTest, ASecondErrorIsRepeatedUntilTheDemuxerIsReset) {
  RecordingOutput output;
  TsDemuxer demuxer(output);
  demuxer.EndOfInput();
  const auto first = demuxer.Step();
  ASSERT_FALSE(first.ok());
  EXPECT_FALSE(demuxer.Step().ok());
  demuxer.Reset(0);
  EXPECT_TRUE(demuxer.Step().ok());
}

TEST(TsDemuxerTest, AppendRejectsAnOffsetThatIsNotTheNextOne) {
  RecordingOutput output;
  TsDemuxer demuxer(output);
  EXPECT_EQ(demuxer.NextOffset(), 0U);
  EXPECT_EQ(demuxer.Append(5, penumbra::SharedBytes::Copy(Bytes{1})).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(demuxer.Append(0, penumbra::SharedBytes::Copy(Bytes{1})).ok());
  EXPECT_EQ(demuxer.NextOffset(), 1U);
}

TEST(TsDemuxerTest, ALongInputYieldsToTheCaller) {
  SyntheticSpec spec;
  spec.frames = 150;
  const Bytes data = Flatten(BuildSynthetic(spec).packets);
  RecordingOutput output;
  TsDemuxerOptions options;
  options.probe_duration = false;
  TsDemuxer demuxer(output, options);
  demuxer.SetSourceInfo(data.size(), false);
  ASSERT_TRUE(demuxer.Append(0, penumbra::SharedBytes::Copy(data)).ok());
  size_t yields = 0;
  for (size_t guard = 0; guard < 10000; ++guard) {
    const auto step = demuxer.Step();
    ASSERT_TRUE(step.ok());
    if (step->kind == StepKind::kYield) {
      ++yields;
    }
    if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  EXPECT_GT(data.size() / 188, kTsStepBudgetPackets);
  EXPECT_GE(yields, (data.size() / 188 / kTsStepBudgetPackets) - 1);
  EXPECT_EQ(output.SamplesOf(kVideoPid).size(), 150U);
}

class PacketSizes : public ::testing::TestWithParam<size_t> {};

TEST_P(PacketSizes, TimecodePrefixesAndParityTrailersAreSkipped) {
  SyntheticSpec spec;
  spec.frames = 20;
  const Synthetic synthetic = BuildSynthetic(spec);
  Playback baseline;
  baseline.Play(Flatten(synthetic.packets));
  Bytes wrapped;
  for (const Bytes& packet : synthetic.packets) {
    if (GetParam() == 192) {
      wrapped.insert(wrapped.end(), 4, 0x00);
    }
    wrapped.insert(wrapped.end(), packet.begin(), packet.end());
    if (GetParam() == 204) {
      wrapped.insert(wrapped.end(), 16, 0x00);
    }
  }
  Playback run;
  run.Play(wrapped);
  ASSERT_TRUE(run.status.ok()) << run.status;
  EXPECT_EQ(RenderAll(run.output), RenderAll(baseline.output));
}

INSTANTIATE_TEST_SUITE_P(Strides, PacketSizes,
                         ::testing::Values(size_t{192}, size_t{204}));

TEST(TsDemuxerRobustnessTest, EveryTruncationEndsInSamplesOrDataLoss) {
  SyntheticSpec spec;
  spec.frames = 8;
  spec.captions = true;
  spec.id3 = true;
  const Bytes data = Flatten(BuildSynthetic(spec).packets);
  for (size_t length = 0; length < data.size(); length += 37) {
    Playback run;
    run.Play(
        Bytes(data.begin(), data.begin() + static_cast<ptrdiff_t>(length)));
    if (!run.status.ok()) {
      EXPECT_TRUE(run.status.code() == absl::StatusCode::kDataLoss ||
                  run.status.code() == absl::StatusCode::kUnimplemented)
          << length << " " << run.status;
    }
  }
}

TEST(TsDemuxerRobustnessTest, EveryCorruptedByteEndsInSamplesOrAnError) {
  SyntheticSpec spec;
  spec.frames = 8;
  spec.captions = true;
  spec.id3 = true;
  const Bytes data = Flatten(BuildSynthetic(spec).packets);
  for (size_t index = 0; index < data.size(); index += 41) {
    for (const uint8_t mask : {uint8_t{0xFF}, uint8_t{0x01}, uint8_t{0x80}}) {
      Bytes damaged = data;
      damaged[index] ^= mask;
      Playback run;
      run.Play(damaged, 1500);
      if (!run.status.ok()) {
        EXPECT_TRUE(run.status.code() == absl::StatusCode::kDataLoss ||
                    run.status.code() == absl::StatusCode::kUnimplemented)
            << index << " " << run.status;
      }
    }
  }
}

TEST(TsDemuxerRobustnessTest, RandomPacketOrderNeverBreaksTheDemuxer) {
  SyntheticSpec spec;
  spec.frames = 12;
  Synthetic synthetic = BuildSynthetic(spec);
  uint32_t state = 12345;
  for (size_t index = synthetic.packets.size() - 1; index > 0; --index) {
    state = (state * 1664525U) + 1013904223U;
    std::swap(synthetic.packets[index], synthetic.packets[state % (index + 1)]);
  }
  Playback run;
  run.Play(Flatten(synthetic.packets), 1000);
  if (!run.status.ok()) {
    EXPECT_TRUE(run.status.code() == absl::StatusCode::kDataLoss ||
                run.status.code() == absl::StatusCode::kUnimplemented);
  }
}

}
}
