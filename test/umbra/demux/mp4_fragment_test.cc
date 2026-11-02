#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "test_support.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_demuxer.h"

namespace umbra::demux::test {
namespace {

constexpr uint32_t kVideoId = 1;
constexpr uint32_t kAudioId = 2;

TrackSpec VideoTrack(size_t count) {
  TrackSpec track;
  track.id = kVideoId;
  track.timescale = 30000;
  track.codec = TestCodec::kAvc;
  for (size_t index = 0; index < count; ++index) {
    SampleSpec sample;
    sample.size = 200 + (static_cast<uint32_t>(index) * 3);
    sample.duration = 1001;
    sample.sync = index % 4 == 0;
    sample.cts = static_cast<int32_t>((index % 2) * 1001);
    track.samples.push_back(sample);
  }
  return track;
}

TrackSpec AudioTrack(size_t count) {
  TrackSpec track;
  track.id = kAudioId;
  track.timescale = 44100;
  track.codec = TestCodec::kAac;
  for (size_t index = 0; index < count; ++index) {
    SampleSpec sample;
    sample.size = 80 + static_cast<uint32_t>(index);
    sample.duration = 1024;
    track.samples.push_back(sample);
  }
  return track;
}

FragmentedSpec TwoTrackSpec() {
  FragmentedSpec spec;
  spec.tracks = {VideoTrack(12), AudioTrack(12)};
  return spec;
}

Bytes DataOf(const penumbra::EncodedSample& sample) {
  const std::span<const uint8_t> data = sample.data.span();
  return {data.begin(), data.end()};
}

std::string Describe(const std::vector<penumbra::EncodedSample>& samples) {
  std::string text;
  for (const penumbra::EncodedSample& sample : samples) {
    text += sample.ToString() + "\n";
  }
  return text;
}

void ExpectTrackMatches(const FragmentedSpec& spec,
                        const RecordingOutput& output, uint32_t track_id,
                        size_t first_sample) {
  const auto found = std::ranges::find_if(
      spec.tracks,
      [&](const TrackSpec& track) { return track.id == track_id; });
  ASSERT_NE(found, spec.tracks.end());
  const auto samples = output.SamplesOf(track_id);
  ASSERT_EQ(samples.size(), found->samples.size() - first_sample);
  int64_t dts = 0;
  for (size_t index = 0; index < first_sample; ++index) {
    dts += found->samples[index].duration;
  }
  for (size_t index = first_sample; index < found->samples.size(); ++index) {
    const penumbra::EncodedSample& sample = samples[index - first_sample];
    const SampleSpec& want = found->samples[index];
    EXPECT_EQ(sample.dts.value, dts) << index;
    EXPECT_EQ(sample.pts.value, dts + want.cts) << index;
    EXPECT_EQ(sample.duration.value, want.duration) << index;
    EXPECT_EQ(sample.key_frame, want.sync) << index;
    EXPECT_EQ(DataOf(sample), SampleBytes(track_id, index, want.size)) << index;
    dts += want.duration;
  }
}

TEST(Mp4FragmentTest, DeliversFragmentedSamplesWithExactTiming) {
  const FragmentedSpec spec = TwoTrackSpec();
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  const DriveResult result = DriveBytes(demuxer, file.bytes, 1000);
  ASSERT_TRUE(result.status.ok()) << result.status;
  EXPECT_EQ(result.seek_requests, 0U);
  EXPECT_EQ(output.stream_announcements, 1U);
  ASSERT_EQ(output.streams.size(), 2U);
  ExpectTrackMatches(spec, output, kVideoId, 0);
  ExpectTrackMatches(spec, output, kAudioId, 0);
  for (const penumbra::EncodedSample& sample : output.samples) {
    EXPECT_FALSE(sample.discontinuity);
    EXPECT_EQ(sample.config_change, nullptr);
  }
  ASSERT_NE(output.seek_map, nullptr);
  EXPECT_FALSE(output.seek_map->IsSeekable());
}

TEST(Mp4FragmentTest, ResumesAtAnyByteBoundary) {
  const FragmentedSpec spec = TwoTrackSpec();
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput reference;
  Mp4Demuxer reference_demuxer(reference);
  ASSERT_TRUE(DriveBytes(reference_demuxer, file.bytes, 1 << 20).status.ok());
  for (const size_t chunk : {size_t{1}, size_t{5}, size_t{97}}) {
    RecordingOutput output;
    Mp4Demuxer demuxer(output);
    const DriveResult result = DriveBytes(demuxer, file.bytes, chunk);
    ASSERT_TRUE(result.status.ok()) << chunk << result.status;
    EXPECT_EQ(Describe(output.samples), Describe(reference.samples)) << chunk;
  }
}

TEST(Mp4FragmentTest, LegacyBaseDataOffsetsContinueAcrossTrackFragments) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.default_base_is_moof = false;
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 333).status.ok());
  ExpectTrackMatches(spec, output, kVideoId, 0);
  ExpectTrackMatches(spec, output, kAudioId, 0);
}

TEST(Mp4FragmentTest, ContinuesWithoutTfdtFromThePreviousFragment) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.with_tfdt = false;
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 500).status.ok());
  ExpectTrackMatches(spec, output, kVideoId, 0);
  ExpectTrackMatches(spec, output, kAudioId, 0);
}

TEST(Mp4FragmentTest, SignedFragmentCompositionOffsetsAreExact) {
  FragmentedSpec spec;
  TrackSpec video = VideoTrack(8);
  video.ctts_version = 1;
  video.samples[0].cts = -2002;
  video.samples[3].cts = -1001;
  spec.tracks = {video};
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 700).status.ok());
  ExpectTrackMatches(spec, output, kVideoId, 0);
  EXPECT_LT(output.samples[0].pts.value, output.samples[0].dts.value);
}

TEST(Mp4FragmentTest, JumpInDecodeTimeSetsTheDiscontinuityFlag) {
  FragmentedSpec spec;
  spec.tracks = {AudioTrack(12)};
  spec.tfdt_override[2] = 900000;
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 4096).status.ok());
  ASSERT_EQ(output.samples.size(), 12U);
  for (size_t index = 0; index < output.samples.size(); ++index) {
    EXPECT_EQ(output.samples[index].discontinuity, index == 8) << index;
  }
  EXPECT_EQ(output.samples[8].dts.value, 900000);
}

TEST(Mp4FragmentTest, SidxBuildsASeekMapAndSeekingResumesAtAFragment) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.with_sidx = true;
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 1 << 20).status.ok());
  ASSERT_NE(output.seek_map, nullptr);
  ASSERT_TRUE(output.seek_map->IsSeekable());
  EXPECT_EQ(output.seek_map->Duration(),
            penumbra::TimeDelta::FromMicroseconds(Micros(12, 1001, 30000)));
  const std::optional<SeekPoint> start =
      output.seek_map->SeekPointFor(penumbra::MediaTime::Zero());
  ASSERT_TRUE(start.has_value());
  EXPECT_EQ(start->byte_offset, file.fragment_offsets[0]);

  const penumbra::MediaTime target =
      penumbra::MediaTime::FromMicroseconds(Micros(5, 1001, 30000));
  const std::optional<SeekPoint> point = output.seek_map->SeekPointFor(target);
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, file.fragment_offsets[1]);
  EXPECT_LE(point->time, target);

  output.samples.clear();
  const absl::StatusOr<uint64_t> offset = demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok());
  EXPECT_EQ(*offset, file.fragment_offsets[1]);
  const DriveResult resumed = DriveBytes(demuxer, file.bytes, 777);
  ASSERT_TRUE(resumed.status.ok()) << resumed.status;
  ASSERT_FALSE(output.samples.empty());
  ASSERT_EQ(output.SamplesOf(kVideoId).size(), 8U);
  EXPECT_TRUE(output.SamplesOf(kVideoId).front().discontinuity);
  EXPECT_TRUE(output.SamplesOf(kAudioId).front().discontinuity);
  EXPECT_EQ(output.SamplesOf(kVideoId).front().dts.value, 4 * 1001);
  ExpectTrackMatches(spec, output, kVideoId, 4);
  ExpectTrackMatches(spec, output, kAudioId, 4);
}

TEST(Mp4FragmentTest, SeekPointBeyondTheEndPicksTheLastFragment) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.with_sidx = true;
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 1 << 20).status.ok());
  const std::optional<SeekPoint> point =
      output.seek_map->SeekPointFor(penumbra::MediaTime::FromSeconds(1000));
  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->byte_offset, file.fragment_offsets[2]);
}

TEST(Mp4FragmentTest, MediaSegmentsAfterAnInitSegmentUseReset) {
  const FragmentedSpec spec = TwoTrackSpec();
  const FragmentedFile file = BuildFragmented(spec);
  const Bytes init(
      file.bytes.begin(),
      file.bytes.begin() + static_cast<std::ptrdiff_t>(file.init_size));
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, init, 1000).status.ok());
  EXPECT_EQ(output.streams.size(), 2U);
  EXPECT_TRUE(output.samples.empty());

  const Bytes second(file.bytes.begin() +
                         static_cast<std::ptrdiff_t>(file.fragment_offsets[1]),
                     file.bytes.begin() +
                         static_cast<std::ptrdiff_t>(file.fragment_offsets[2]));
  demuxer.Reset(0);
  ASSERT_TRUE(DriveBytes(demuxer, second, 1000).status.ok());
  const auto video = output.SamplesOf(kVideoId);
  ASSERT_EQ(video.size(), 4U);
  EXPECT_EQ(video[0].dts.value, 4 * 1001);
  EXPECT_TRUE(video[0].discontinuity);
  EXPECT_EQ(DataOf(video[0]),
            SampleBytes(kVideoId, 4, spec.tracks[0].samples[4].size));
}

TEST(Mp4FragmentTest, EmsgVersion1CarriesAbsoluteTimeAndPayload) {
  FragmentedSpec spec = TwoTrackSpec();
  const Bytes message = {1, 2, 3, 4};
  spec.boxes_before_fragment[1] =
      EmsgV1("urn:test:scheme", "7", 1000, 5000, 2000, 99, message);
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 211).status.ok());
  ASSERT_EQ(output.metadata_streams.size(), 1U);
  EXPECT_EQ(output.metadata_streams[0].kind, MetadataKind::kEmsg);
  EXPECT_EQ(output.metadata_streams[0].scheme, "urn:test:scheme");
  EXPECT_EQ(output.metadata_streams[0].value, "7");
  ASSERT_EQ(output.items.size(), 1U);
  const TimedMetadataItem& item = output.items[0];
  EXPECT_EQ(item.time.value, 5000);
  EXPECT_EQ(item.time.time_base, penumbra::Rational::Of(1, 1000));
  EXPECT_EQ(item.duration.value, 2000);
  EXPECT_EQ(item.event_id, 99U);
  const std::span<const uint8_t> payload = item.payload.span();
  EXPECT_EQ(Bytes(payload.begin(), payload.end()), message);
  EXPECT_EQ(item.stream_id, output.metadata_streams[0].stream_id);
}

TEST(Mp4FragmentTest, EmsgVersion0IsRelativeToTheNextFragment) {
  FragmentedSpec spec;
  spec.tracks = {AudioTrack(12)};
  spec.boxes_before_fragment[2] =
      EmsgV0("urn:test:scheme", "", 1000, 250, 0xFFFFFFFFU, 5, Bytes{9});
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 300).status.ok());
  ASSERT_EQ(output.items.size(), 1U);
  const TimedMetadataItem& item = output.items[0];
  const int64_t fragment_start_us = Micros(8, 1024, 44100);
  const int64_t expected_ms = ((fragment_start_us + 500) / 1000) + 250;
  EXPECT_NEAR(static_cast<double>(item.time.value),
              static_cast<double>(expected_ms), 1.0);
  EXPECT_FALSE(item.duration.is_known());
  EXPECT_EQ(item.event_id, 5U);
}

TEST(Mp4FragmentTest, Id3EmsgUsesTheId3StreamKind) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.boxes_before_fragment[0] =
      EmsgV1("https://aomedia.org/emsg/ID3", "", 90000, 0, 0, 1,
             Bytes{'I', 'D', '3', 4, 0});
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 4096).status.ok());
  ASSERT_EQ(output.metadata_streams.size(), 1U);
  EXPECT_EQ(output.metadata_streams[0].kind, MetadataKind::kId3);
}

TEST(Mp4FragmentTest, EmsgWithAnUnknownVersionIsSkipped) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.boxes_before_fragment[0] = FullBox("emsg", 5, 0, Bytes(20, 0));
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 4096).status.ok());
  EXPECT_TRUE(output.items.empty());
  EXPECT_EQ(demuxer.Stats().unsupported_emsg_boxes, 1U);
}

TEST(Mp4FragmentTest, ProducerReferenceTimeReachesTheOutput) {
  FragmentedSpec spec = TwoTrackSpec();
  spec.boxes_before_fragment[0] =
      PrftBox(kVideoId, 0xE000000012345678ULL, 4004, 0);
  spec.boxes_before_fragment[1] =
      PrftBox(kAudioId, 0xE000000022345678ULL, 5000000000ULL, 1);
  const FragmentedFile file = BuildFragmented(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(DriveBytes(demuxer, file.bytes, 4096).status.ok());
  ASSERT_EQ(output.references.size(), 2U);
  EXPECT_EQ(output.references[0].track_id, kVideoId);
  EXPECT_EQ(output.references[0].ntp_timestamp, 0xE000000012345678ULL);
  EXPECT_EQ(output.references[0].media_time.value, 4004);
  EXPECT_EQ(output.references[0].media_time.time_base,
            penumbra::Rational::Of(1, 30000));
  EXPECT_EQ(output.references[0].flags, 8U);
  EXPECT_EQ(output.references[1].track_id, kAudioId);
  EXPECT_EQ(output.references[1].media_time.value, 5000000000LL);
}

TEST(Mp4FragmentTest, MoofBeforeMoovIsRejected) {
  const FragmentedSpec spec = TwoTrackSpec();
  const FragmentedFile file = BuildFragmented(spec);
  const Bytes without_init(file.bytes.begin() + static_cast<std::ptrdiff_t>(
                                                    file.fragment_offsets[0]),
                           file.bytes.end());
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  EXPECT_EQ(DriveBytes(demuxer, without_init, 4096).status.code(),
            absl::StatusCode::kDataLoss);
}

TEST(Mp4FragmentTest, EveryTruncationEndsInAnErrorOrSamples) {
  FragmentedSpec spec;
  spec.tracks = {VideoTrack(4), AudioTrack(4)};
  spec.samples_per_fragment = 2;
  spec.fragments = 2;
  spec.with_sidx = true;
  spec.boxes_before_fragment[1] =
      EmsgV0("urn:x", "v", 1000, 5, 10, 1, Bytes{1, 2, 3});
  const FragmentedFile file = BuildFragmented(spec);
  for (size_t length = 0; length < file.bytes.size(); length += 3) {
    const Bytes prefix(
        file.bytes.begin(),
        file.bytes.begin() + static_cast<std::ptrdiff_t>(length));
    RecordingOutput output;
    Mp4Demuxer demuxer(output);
    const DriveResult result = DriveBytes(demuxer, prefix, 64);
    EXPECT_TRUE(result.status.ok() ||
                result.status.code() == absl::StatusCode::kDataLoss)
        << length << " " << result.status;
  }
}

TEST(Mp4FragmentTest, EveryByteCorruptionIsHandledWithoutCrashing) {
  FragmentedSpec spec;
  spec.tracks = {VideoTrack(2), AudioTrack(2)};
  spec.samples_per_fragment = 2;
  spec.fragments = 1;
  spec.with_sidx = true;
  const FragmentedFile file = BuildFragmented(spec);
  for (size_t position = 0; position < file.bytes.size(); ++position) {
    for (const uint8_t value : {uint8_t{0}, uint8_t{0xFF}, uint8_t{0x7F}}) {
      Bytes corrupt = file.bytes;
      corrupt[position] = value;
      RecordingOutput output;
      Mp4Demuxer demuxer(output);
      const DriveResult result = DriveBytes(demuxer, corrupt, 128);
      EXPECT_TRUE(result.status.ok() ||
                  result.status.code() == absl::StatusCode::kDataLoss ||
                  result.status.code() == absl::StatusCode::kUnimplemented)
          << position << " " << result.status;
    }
  }
}

}
}
