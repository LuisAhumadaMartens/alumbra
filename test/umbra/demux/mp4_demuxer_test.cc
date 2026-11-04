#include "umbra/demux/mp4_demuxer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_seek_map.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux::test {
namespace {

TEST(IndexedSeekMapTest, NearestPointSelectsTheCloserSyncPoint) {
  const std::vector<SeekPoint> points = {
      {.time = penumbra::MediaTime::FromSeconds(0), .byte_offset = 10},
      {.time = penumbra::MediaTime::FromSeconds(2), .byte_offset = 20},
      {.time = penumbra::MediaTime::FromSeconds(4), .byte_offset = 40},
  };
  const IndexedSeekMap map(points, penumbra::TimeDelta::FromSeconds(5));

  EXPECT_EQ(
      map.NearestSeekPointFor(penumbra::MediaTime::FromMilliseconds(1200)),
      points[1]);
  EXPECT_EQ(map.NearestSeekPointFor(penumbra::MediaTime::FromMilliseconds(800)),
            points[0]);
  EXPECT_EQ(map.NearestSeekPointFor(penumbra::MediaTime::FromSeconds(8)),
            points[2]);
}

TEST(ProgressiveSeekMapTest, NearestPointChoosesAReferenceTrackSyncSample) {
  auto samples = std::make_shared<SampleList>(SampleList{
      {.offset = 100,
       .size = 0,
       .duration = 0,
       .dts = 0,
       .cts_offset = 0,
       .description = 0,
       .sync = true},
      {.offset = 200,
       .size = 0,
       .duration = 0,
       .dts = 10,
       .cts_offset = 0,
       .description = 0,
       .sync = false},
      {.offset = 300,
       .size = 0,
       .duration = 0,
       .dts = 20,
       .cts_offset = 0,
       .description = 0,
       .sync = true},
      {.offset = 400,
       .size = 0,
       .duration = 0,
       .dts = 30,
       .cts_offset = 0,
       .description = 0,
       .sync = false},
      {.offset = 500,
       .size = 0,
       .duration = 0,
       .dts = 40,
       .cts_offset = 0,
       .description = 0,
       .sync = true},
  });
  const ProgressiveSeekMap map({{.samples = samples,
                                 .shift = 0,
                                 .time_base = penumbra::Rational::Of(1, 10),
                                 .reference = true}},
                               penumbra::TimeDelta::FromSeconds(5));

  const auto point =
      map.NearestSeekPointFor(penumbra::MediaTime::FromMilliseconds(3100));

  ASSERT_TRUE(point.has_value());
  EXPECT_EQ(point->time, penumbra::MediaTime::FromSeconds(4));
  EXPECT_EQ(point->byte_offset, 500U);
}

constexpr uint32_t kVideoId = 1;
constexpr uint32_t kAudioId = 2;

TrackSpec AacTrack(uint32_t id, size_t count, uint32_t size = 120) {
  TrackSpec track;
  track.id = id;
  track.timescale = 44100;
  track.codec = TestCodec::kAac;
  for (size_t index = 0; index < count; ++index) {
    SampleSpec sample;
    sample.size = size + static_cast<uint32_t>(index);
    sample.duration = 1024;
    track.samples.push_back(sample);
  }
  return track;
}

TrackSpec AvcTrack(uint32_t id, size_t count, uint32_t size = 400) {
  TrackSpec track;
  track.id = id;
  track.timescale = 30000;
  track.codec = TestCodec::kAvc;
  for (size_t index = 0; index < count; ++index) {
    SampleSpec sample;
    sample.size = size + (static_cast<uint32_t>(index) * 5);
    sample.duration = 1001;
    sample.sync = index % 4 == 0;
    track.samples.push_back(sample);
  }
  return track;
}

std::string Describe(const std::vector<penumbra::EncodedSample>& samples) {
  std::string text;
  for (const penumbra::EncodedSample& sample : samples) {
    text += sample.ToString() + "\n";
  }
  return text;
}

struct Parsed {
  Parsed(const BuiltFile& file, size_t chunk, Mp4DemuxerOptions options = {}) {
    Mp4Demuxer demuxer(output, options);
    result = Drive(demuxer, file, chunk);
  }

  RecordingOutput output;
  DriveResult result;
};

void ExpectSamplesMatchFile(const BuiltFile& file,
                            const std::vector<penumbra::EncodedSample>& got) {
  ASSERT_EQ(got.size(), file.expected.size());
  for (size_t index = 0; index < got.size(); ++index) {
    EXPECT_EQ(got[index].track_id, file.expected[index].track_id) << index;
    const std::span<const uint8_t> data = got[index].data.span();
    EXPECT_EQ(Bytes(data.begin(), data.end()), file.expected[index].data)
        << index;
  }
}

TEST(Mp4DemuxerProgressiveTest, ReadsHdrStaticMetadataAndDolbyVisionConfig) {
  TrackSpec video = AvcTrack(kVideoId, 1);
  Writer mastering;
  mastering.U16(34000)
      .U16(16000)
      .U16(13250)
      .U16(34500)
      .U16(7500)
      .U16(3000)
      .U16(15635)
      .U16(16450)
      .U32(10'000'000)
      .U32(50);
  Writer light;
  light.U16(1000).U16(400);
  video.extra_video_boxes.push_back(Box("mdcv", mastering.Data()));
  video.extra_video_boxes.push_back(Box("clli", light.Data()));
  video.extra_video_boxes.push_back(
      Box("dvcC", Bytes{1, 0, static_cast<uint8_t>(8 << 1), 0x25, 0x40}));
  ProgressiveSpec spec;
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 17);

  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  ASSERT_EQ(parsed.output.streams.size(), 1U);
  const auto& info =
      std::get<penumbra::VideoInfo>(parsed.output.streams.front().format);
  ASSERT_TRUE(info.hdr_static_metadata.mastering_display.has_value());
  EXPECT_EQ(info.hdr_static_metadata.mastering_display->display_primaries_x[0],
            34000);
  EXPECT_EQ(info.hdr_static_metadata.mastering_display->max_luminance,
            10'000'000U);
  EXPECT_EQ(info.hdr_static_metadata.mastering_display->min_luminance, 50U);
  EXPECT_EQ(info.hdr_static_metadata.max_content_light_level, 1000);
  EXPECT_EQ(info.hdr_static_metadata.max_frame_average_light_level, 400);
  ASSERT_TRUE(info.dolby_vision.has_value());
  EXPECT_EQ(info.dolby_vision->profile, 8);
  EXPECT_EQ(info.dolby_vision->level, 4);
  EXPECT_EQ(info.dolby_vision->base_layer_compatibility_id, 4);
}

TEST(Mp4DemuxerProgressiveTest, ParsesDolbyVisionProfilesAndLevels) {
  const std::array<std::array<uint8_t, 3>, 3> configurations = {
      std::array<uint8_t, 3>{5, 6, 0}, std::array<uint8_t, 3>{8, 1, 1},
      std::array<uint8_t, 3>{8, 4, 4}};
  for (const auto& [profile, level, compatibility] : configurations) {
    TrackSpec video = AvcTrack(kVideoId, 1);
    const auto packed_profile =
        static_cast<uint8_t>((profile << 1) | (level >> 5));
    const auto packed_level =
        static_cast<uint8_t>(((level & 0x1fU) << 3) | 0x05U);
    video.extra_video_boxes.push_back(
        Box("dvcC", Bytes{1, 0, packed_profile, packed_level,
                          static_cast<uint8_t>(compatibility << 4)}));
    ProgressiveSpec spec;
    spec.tracks = {video};
    const Parsed parsed(BuildProgressive(spec), 19);
    ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
    ASSERT_EQ(parsed.output.streams.size(), 1U);
    const auto& info =
        std::get<penumbra::VideoInfo>(parsed.output.streams.front().format);
    ASSERT_TRUE(info.dolby_vision.has_value());
    EXPECT_EQ(info.dolby_vision->profile, profile);
    EXPECT_EQ(info.dolby_vision->level, level);
    EXPECT_EQ(info.dolby_vision->base_layer_compatibility_id, compatibility);
  }
}

TEST(Mp4DemuxerProgressiveTest, DeliversSamplesInFileOrderWithExactTiming) {
  ProgressiveSpec spec;
  TrackSpec video = AvcTrack(kVideoId, 8);
  video.use_ctts = true;
  for (size_t index = 0; index < video.samples.size(); ++index) {
    video.samples[index].cts = static_cast<int32_t>((index % 2) * 1001);
  }
  spec.tracks = {video, AacTrack(kAudioId, 10)};
  const BuiltFile file = BuildProgressive(spec);
  const Parsed parsed(file, 4096);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  ASSERT_EQ(parsed.output.streams.size(), 2U);
  EXPECT_EQ(parsed.output.streams[0].codec, penumbra::CodecId::kH264);
  EXPECT_EQ(parsed.output.streams[0].nal_unit_framing,
            penumbra::NalUnitFraming::kLengthPrefixed);
  EXPECT_EQ(parsed.output.streams[0].nal_length_size, 4U);
  EXPECT_EQ(parsed.output.streams[0].codec_string, "avc1.640028");
  EXPECT_EQ(parsed.output.streams[0].time_base,
            penumbra::Rational::Of(1, 30000));
  EXPECT_EQ(parsed.output.streams[0].language, "und");
  EXPECT_EQ(parsed.output.streams[1].codec, penumbra::CodecId::kAac);
  EXPECT_EQ(parsed.output.streams[1].aac_framing, penumbra::AacFraming::kRaw);
  EXPECT_EQ(parsed.output.streams[1].codec_string, "mp4a.40.2");
  EXPECT_EQ(parsed.output.streams[1].time_base,
            penumbra::Rational::Of(1, 44100));
  const auto* audio_info =
      std::get_if<penumbra::AudioInfo>(&parsed.output.streams[1].format);
  ASSERT_NE(audio_info, nullptr);
  EXPECT_EQ(audio_info->sample_rate, 44100);
  EXPECT_EQ(audio_info->channel_count, 2);
  ExpectSamplesMatchFile(file, parsed.output.samples);

  const auto video_samples = parsed.output.SamplesOf(kVideoId);
  ASSERT_EQ(video_samples.size(), 8U);
  for (size_t index = 0; index < video_samples.size(); ++index) {
    const penumbra::Rational base = penumbra::Rational::Of(1, 30000);
    EXPECT_EQ(video_samples[index].dts.value,
              static_cast<int64_t>(index) * 1001);
    EXPECT_EQ(video_samples[index].dts.time_base, base);
    EXPECT_EQ(video_samples[index].pts.value,
              (static_cast<int64_t>(index) * 1001) +
                  static_cast<int64_t>((index % 2) * 1001));
    EXPECT_EQ(video_samples[index].duration.value, 1001);
    EXPECT_EQ(video_samples[index].key_frame, index % 4 == 0);
    EXPECT_FALSE(video_samples[index].discontinuity);
    EXPECT_FALSE(video_samples[index].trim_start.is_known());
  }
  const auto audio_samples = parsed.output.SamplesOf(kAudioId);
  ASSERT_EQ(audio_samples.size(), 10U);
  EXPECT_TRUE(audio_samples[3].key_frame);
  EXPECT_EQ(audio_samples[3].pts.value, 3 * 1024);
}

TEST(Mp4DemuxerProgressiveTest, ResumesAtAnyByteBoundary) {
  ProgressiveSpec spec;
  spec.tracks = {AvcTrack(kVideoId, 6, 40), AacTrack(kAudioId, 7, 30)};
  spec.tracks[1].samples_per_chunk = 3;
  const BuiltFile file = BuildProgressive(spec);
  const Parsed reference(file, 1 << 20);
  ASSERT_TRUE(reference.result.status.ok()) << reference.result.status;
  for (const size_t chunk : {size_t{1}, size_t{3}, size_t{7}, size_t{64}}) {
    const Parsed parsed(file, chunk);
    ASSERT_TRUE(parsed.result.status.ok()) << chunk << parsed.result.status;
    EXPECT_EQ(Describe(parsed.output.samples),
              Describe(reference.output.samples))
        << chunk;
    ExpectSamplesMatchFile(file, parsed.output.samples);
  }
}

TEST(Mp4DemuxerProgressiveTest, SampleDataIsASliceOfTheAppendedBuffer) {
  ProgressiveSpec spec;
  spec.tracks = {AacTrack(kAudioId, 5)};
  const BuiltFile file = BuildProgressive(spec);
  const Bytes bytes = file.Flatten();
  const penumbra::SharedBytes whole = penumbra::SharedBytes::Copy(bytes);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  ASSERT_TRUE(demuxer.Append(0, whole).ok());
  demuxer.EndOfInput();
  for (int guard = 0; guard < 100; ++guard) {
    const absl::StatusOr<StepResult> step = demuxer.Step();
    ASSERT_TRUE(step.ok()) << step.status();
    if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  ASSERT_EQ(output.samples.size(), 5U);
  for (const penumbra::EncodedSample& sample : output.samples) {
    const auto begin = reinterpret_cast<uintptr_t>(sample.data.data());
    const auto whole_begin = reinterpret_cast<uintptr_t>(whole.data());
    EXPECT_GE(begin, whole_begin);
    EXPECT_LE(begin + sample.data.size(), whole_begin + whole.size());
  }
}

TEST(Mp4DemuxerProgressiveTest, MoovAtTheEndNeedsFewRangeRequests) {
  ProgressiveSpec spec;
  spec.moov_at_end = true;
  spec.tracks = {AvcTrack(kVideoId, 20, 50000), AacTrack(kAudioId, 40, 2000)};
  const BuiltFile file = BuildProgressive(spec);
  ASSERT_GT(file.size, 1000000U);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  const DriveResult first =
      Drive(demuxer, file, 65536, StopAfter{.output = &output, .samples = 1});
  ASSERT_TRUE(first.status.ok()) << first.status;
  EXPECT_EQ(first.seek_requests, 2U);
  EXPECT_LT(first.bytes_fetched, (uint64_t{4} * 65536) + file.moov_size);
  ASSERT_FALSE(output.samples.empty());
  EXPECT_EQ(output.streams.size(), 2U);
  EXPECT_TRUE(output.seek_map != nullptr && output.seek_map->IsSeekable());

  const DriveResult rest = Drive(demuxer, file, 65536);
  ASSERT_TRUE(rest.status.ok()) << rest.status;
  ExpectSamplesMatchFile(file, output.samples);
}

TEST(Mp4DemuxerProgressiveTest, MoovAtTheEndFedByteByByteStillSeeks) {
  ProgressiveSpec spec;
  spec.moov_at_end = true;
  spec.tracks = {AacTrack(kAudioId, 4, 40)};
  const BuiltFile file = BuildProgressive(spec);
  const Parsed parsed(file, 5);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  ExpectSamplesMatchFile(file, parsed.output.samples);
}

TEST(Mp4DemuxerProgressiveTest, Co64OffsetsBeyondFourGibAreRead) {
  ProgressiveSpec spec;
  spec.data_shift = uint64_t{5} << 30;
  TrackSpec audio = AacTrack(kAudioId, 6, 90);
  audio.co64 = true;
  spec.tracks = {audio};
  const BuiltFile file = BuildProgressive(spec);
  ASSERT_GT(file.size, uint64_t{5} << 30);
  const Parsed parsed(file, 4096);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  EXPECT_EQ(parsed.result.seek_requests, 1U);
  ExpectSamplesMatchFile(file, parsed.output.samples);
  EXPECT_GT(file.expected.front().offset, uint64_t{1} << 32);
}

TEST(Mp4DemuxerProgressiveTest, ReadsStz2AndMultipleChunkRuns) {
  ProgressiveSpec spec;
  TrackSpec audio = AacTrack(kAudioId, 8, 60);
  audio.stz2 = true;
  audio.samples_per_chunk = 3;
  spec.tracks = {audio};
  const BuiltFile file = BuildProgressive(spec);
  const Parsed parsed(file, 100);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  ExpectSamplesMatchFile(file, parsed.output.samples);
}

TEST(Mp4DemuxerProgressiveTest, InterleavesTracksByFileOffset) {
  ProgressiveSpec spec;
  spec.tracks = {AvcTrack(kVideoId, 8, 40), AacTrack(kAudioId, 12, 30)};
  spec.tracks[0].samples_per_chunk = 2;
  spec.tracks[1].samples_per_chunk = 3;
  const BuiltFile file = BuildProgressive(spec);
  const Parsed parsed(file, 512);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  EXPECT_EQ(parsed.result.seek_requests, 0U);
  ExpectSamplesMatchFile(file, parsed.output.samples);
}

TEST(Mp4DemuxerProgressiveTest, YieldsWhenTheStepBudgetIsUsed) {
  ProgressiveSpec spec;
  spec.tracks = {AacTrack(kAudioId, 600, 4)};
  const BuiltFile file = BuildProgressive(spec);
  const Bytes bytes = file.Flatten();
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(demuxer.Append(0, penumbra::SharedBytes::Copy(bytes)).ok());
  demuxer.EndOfInput();
  size_t yields = 0;
  while (true) {
    const absl::StatusOr<StepResult> step = demuxer.Step();
    ASSERT_TRUE(step.ok()) << step.status();
    if (step->kind == StepKind::kYield) {
      ++yields;
      EXPECT_LE(output.samples.size(), yields * kStepBudgetUnits);
    }
    if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  EXPECT_GE(yields, 4U);
  EXPECT_EQ(output.samples.size(), 600U);
}

TEST(Mp4DemuxerProgressiveTest, AppendMustContinueAtTheNextOffset) {
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  EXPECT_EQ(demuxer.NextOffset(), 0U);
  EXPECT_EQ(demuxer.Append(5, penumbra::SharedBytes::Copy(Bytes{1, 2})).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(demuxer.Append(0, penumbra::SharedBytes::Copy(Bytes{1, 2})).ok());
  EXPECT_EQ(demuxer.NextOffset(), 2U);
}

TEST(Mp4DemuxerProgressiveTest, SkipsUnsupportedTracks) {
  ProgressiveSpec spec;
  TrackSpec text = AacTrack(7, 3, 20);
  text.codec = TestCodec::kUnknown;
  spec.tracks = {AacTrack(kAudioId, 4, 50), text};
  const BuiltFile file = BuildProgressive(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  const DriveResult result = Drive(demuxer, file, 1024);
  ASSERT_TRUE(result.status.ok()) << result.status;
  EXPECT_EQ(output.streams.size(), 1U);
  EXPECT_EQ(output.samples.size(), 4U);
  EXPECT_EQ(demuxer.Stats().skipped_tracks, 1U);
}

TEST(Mp4DemuxerProgressiveTest, FileWithOnlyUnsupportedTracksIsRejected) {
  ProgressiveSpec spec;
  TrackSpec text = AacTrack(7, 3, 20);
  text.codec = TestCodec::kUnknown;
  spec.tracks = {text};
  const Parsed parsed(BuildProgressive(spec), 1024);
  EXPECT_EQ(parsed.result.status.code(), absl::StatusCode::kUnimplemented);
  EXPECT_TRUE(parsed.output.samples.empty());
}

TEST(Mp4DemuxerProgressiveTest, SeekStartsAtTheKeyFrameBeforeTheTarget) {
  ProgressiveSpec spec;
  spec.tracks = {AvcTrack(kVideoId, 12, 60), AacTrack(kAudioId, 20, 30)};
  spec.tracks[0].samples_per_chunk = 2;
  spec.tracks[1].samples_per_chunk = 4;
  const BuiltFile file = BuildProgressive(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(Drive(demuxer, file, 4096).status.ok());
  const size_t total = output.samples.size();
  ASSERT_NE(output.seek_map, nullptr);
  const penumbra::MediaTime target =
      penumbra::MediaTime::FromMicroseconds(Micros(6, 1001, 30000) + 10);
  const std::optional<SeekPoint> point = output.seek_map->SeekPointFor(target);
  ASSERT_TRUE(point.has_value());
  EXPECT_LE(point->time, target);
  EXPECT_EQ(point->time,
            penumbra::MediaTime::FromMicroseconds(Micros(4, 1001, 30000) + 1));
  output.samples.clear();
  const absl::StatusOr<uint64_t> offset = demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok());
  EXPECT_EQ(demuxer.NextOffset(), *offset);
  ASSERT_TRUE(Drive(demuxer, file, 4096).status.ok());
  ASSERT_FALSE(output.samples.empty());
  EXPECT_LT(output.samples.size(), total);
  const auto video = output.SamplesOf(kVideoId);
  ASSERT_FALSE(video.empty());
  EXPECT_EQ(video.front().dts.value, 4 * 1001);
  EXPECT_TRUE(video.front().key_frame);
  EXPECT_TRUE(video.front().discontinuity);
  const auto audio = output.SamplesOf(kAudioId);
  ASSERT_FALSE(audio.empty());
  EXPECT_TRUE(audio.front().discontinuity);
  EXPECT_LE(audio.front().pts.value, (4 * 1001 * 44100) / 30000);
}

TEST(Mp4DemuxerProgressiveTest, SeekBeforeTheMovieIsKnownFails) {
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  EXPECT_EQ(demuxer.Seek(SeekPoint{}).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(Mp4DemuxerProgressiveTest, SeekMapReportsTheMovieDuration) {
  ProgressiveSpec spec;
  spec.tracks = {AacTrack(kAudioId, 3, 10)};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_NE(parsed.output.seek_map, nullptr);
  EXPECT_TRUE(parsed.output.seek_map->IsSeekable());
}

TEST(Mp4DemuxerEditListTest, AacPrimingBecomesTrimStart) {
  ProgressiveSpec spec;
  spec.movie_timescale = 44100;
  TrackSpec audio = AacTrack(kAudioId, 10);
  audio.edits = {MakeEdit(8704, 1024)};
  spec.tracks = {audio};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  const auto samples = parsed.output.SamplesOf(kAudioId);
  ASSERT_EQ(samples.size(), 10U);
  EXPECT_EQ(samples[0].pts.value, -1024);
  EXPECT_EQ(samples[0].dts.value, -1024);
  EXPECT_EQ(samples[0].trim_start.value, 1024);
  EXPECT_EQ(samples[0].trim_start.time_base, penumbra::Rational::Of(1, 44100));
  EXPECT_FALSE(samples[0].trim_end.is_known());
  EXPECT_EQ(samples[1].pts.value, 0);
  EXPECT_FALSE(samples[1].trim_start.is_known());
  EXPECT_FALSE(samples[8].trim_end.is_known());
  EXPECT_EQ(samples[9].pts.value, 8192);
  EXPECT_EQ(samples[9].trim_end.value, 512);
}

TEST(Mp4DemuxerEditListTest, PartialPrimingTrimsPartOfTheFirstSample) {
  ProgressiveSpec spec;
  spec.movie_timescale = 44100;
  TrackSpec audio = AacTrack(kAudioId, 6);
  audio.edits = {MakeEdit(4000, 600)};
  spec.tracks = {audio};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kAudioId);
  EXPECT_EQ(samples[0].pts.value, -600);
  EXPECT_EQ(samples[0].trim_start.value, 600);
  EXPECT_EQ(samples[1].pts.value, 424);
  EXPECT_FALSE(samples[1].trim_start.is_known());
}

TEST(Mp4DemuxerEditListTest, LeadingEmptyEditBecomesADelay) {
  ProgressiveSpec spec;
  spec.movie_timescale = 1000;
  TrackSpec audio = AacTrack(kAudioId, 4);
  audio.edits = {MakeEdit(500, -1), MakeEdit(0, 0)};
  spec.tracks = {audio};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kAudioId);
  EXPECT_EQ(samples[0].pts.value, 22050);
  EXPECT_EQ(samples[1].pts.value, 22050 + 1024);
  EXPECT_FALSE(samples[0].trim_start.is_known());
}

TEST(Mp4DemuxerEditListTest, DelayAndStartOffsetCombine) {
  ProgressiveSpec spec;
  spec.movie_timescale = 1000;
  TrackSpec video = AvcTrack(kVideoId, 4);
  video.edits = {MakeEdit(1000, -1), MakeEdit(100, 2002)};
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kVideoId);
  EXPECT_EQ(samples[0].dts.value, 30000 - 2002);
  EXPECT_EQ(samples[2].dts.value, 30000);
  EXPECT_FALSE(samples[0].trim_start.is_known());
}

TEST(Mp4DemuxerEditListTest, OnlyTheFirstNonEmptyEditIsApplied) {
  ProgressiveSpec spec;
  spec.movie_timescale = 44100;
  TrackSpec audio = AacTrack(kAudioId, 6);
  audio.edits = {MakeEdit(2048, 1024), MakeEdit(2048, 4096), MakeEdit(100, 0)};
  spec.tracks = {audio};
  const BuiltFile file = BuildProgressive(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(Drive(demuxer, file, 4096).status.ok());
  EXPECT_EQ(demuxer.Stats().ignored_edit_entries, 2U);
  EXPECT_EQ(output.samples[0].pts.value, -1024);
  EXPECT_EQ(output.samples[3].trim_end.value, 1024);
  EXPECT_EQ(output.samples[2].trim_end.is_known(), false);
}

TEST(Mp4DemuxerEditListTest, Version1EditListsAreRead) {
  ProgressiveSpec spec;
  spec.movie_timescale = 44100;
  TrackSpec audio = AacTrack(kAudioId, 4);
  audio.elst_version = 1;
  audio.edits = {MakeEdit(4000, 1024)};
  spec.tracks = {audio};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  EXPECT_EQ(parsed.output.samples[0].pts.value, -1024);
  EXPECT_EQ(parsed.output.samples[0].trim_start.value, 1024);
}

TEST(Mp4DemuxerEditListTest, DisablingEditListsKeepsTheMediaTimeline) {
  ProgressiveSpec spec;
  spec.movie_timescale = 44100;
  TrackSpec audio = AacTrack(kAudioId, 4);
  audio.edits = {MakeEdit(2000, 1024)};
  spec.tracks = {audio};
  Mp4DemuxerOptions options;
  options.edit_lists_enabled = false;
  const Parsed parsed(BuildProgressive(spec), 4096, options);
  ASSERT_TRUE(parsed.result.status.ok());
  EXPECT_EQ(parsed.output.samples[0].pts.value, 0);
  EXPECT_FALSE(parsed.output.samples[0].trim_start.is_known());
  EXPECT_FALSE(parsed.output.samples[3].trim_end.is_known());
}

TEST(Mp4DemuxerEditListTest, VideoShiftsWithoutTrimming) {
  ProgressiveSpec spec;
  spec.movie_timescale = 30000;
  TrackSpec video = AvcTrack(kVideoId, 6);
  video.use_ctts = true;
  for (SampleSpec& sample : video.samples) {
    sample.cts = 2002;
  }
  video.edits = {MakeEdit(6006, 2002)};
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kVideoId);
  EXPECT_EQ(samples[0].dts.value, -2002);
  EXPECT_EQ(samples[0].pts.value, 0);
  EXPECT_EQ(samples[1].pts.value, 1001);
  for (const penumbra::EncodedSample& sample : samples) {
    EXPECT_FALSE(sample.trim_start.is_known());
    EXPECT_FALSE(sample.trim_end.is_known());
  }
}

TEST(Mp4DemuxerCompositionTest, SignedVersion1OffsetsAreCarriedExactly) {
  ProgressiveSpec spec;
  TrackSpec video = AvcTrack(kVideoId, 5);
  video.use_ctts = true;
  video.ctts_version = 1;
  const std::vector<int32_t> offsets = {-2002, 0, 3003, -1001, 1001};
  for (size_t index = 0; index < offsets.size(); ++index) {
    video.samples[index].cts = offsets[index];
  }
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kVideoId);
  for (size_t index = 0; index < offsets.size(); ++index) {
    EXPECT_EQ(samples[index].dts.value, static_cast<int64_t>(index) * 1001);
    EXPECT_EQ(samples[index].pts.value,
              (static_cast<int64_t>(index) * 1001) + offsets[index]);
  }
  EXPECT_LT(samples[0].pts.value, samples[0].dts.value);
}

TEST(Mp4DemuxerCompositionTest, Version0OffsetsAboveInt32AreReadAsSigned) {
  ProgressiveSpec spec;
  TrackSpec video = AvcTrack(kVideoId, 3);
  video.use_ctts = true;
  video.samples[0].cts = 2002;
  video.samples[1].cts = -1001;
  video.samples[2].cts = 0;
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok());
  const auto samples = parsed.output.SamplesOf(kVideoId);
  EXPECT_EQ(samples[0].pts.value, 2002);
  EXPECT_EQ(samples[1].pts.value, 1001 - 1001);
}

TEST(Mp4DemuxerCaptionTest, SeiCaptionsLeaveAsTheirOwnStream) {
  Bytes sei_payload = {0xB5, 0x00, 0x31, 'G',  'A',  '9',  '4',  0x03, 0x42,
                       0xFF, 0xFC, 0x80, 0x80, 0xFD, 0x81, 0x82, 0xFF};
  Bytes sei_nal = {0x06, 0x04, static_cast<uint8_t>(sei_payload.size())};
  sei_nal.insert(sei_nal.end(), sei_payload.begin(), sei_payload.end());
  sei_nal.push_back(0x80);
  const Bytes slice = {0x65, 1, 2, 3, 4};
  Writer sample;
  sample.U32(static_cast<uint32_t>(sei_nal.size())).Add(sei_nal);
  sample.U32(static_cast<uint32_t>(slice.size())).Add(slice);
  ProgressiveSpec spec;
  TrackSpec video = AvcTrack(kVideoId, 3, 50);
  video.samples[1].size = static_cast<uint32_t>(sample.Data().size());
  video.data_override[1] = sample.Data();
  spec.tracks = {video};
  const Parsed parsed(BuildProgressive(spec), 4096);
  ASSERT_TRUE(parsed.result.status.ok()) << parsed.result.status;
  ASSERT_EQ(parsed.output.metadata_streams.size(), 1U);
  EXPECT_EQ(parsed.output.metadata_streams[0].kind, MetadataKind::kCaptions);
  EXPECT_EQ(parsed.output.metadata_streams[0].source_track_id, kVideoId);
  ASSERT_EQ(parsed.output.items.size(), 1U);
  const TimedMetadataItem& item = parsed.output.items[0];
  EXPECT_EQ(item.stream_id, parsed.output.metadata_streams[0].stream_id);
  EXPECT_EQ(item.time, parsed.output.samples[1].pts);
  const std::span<const uint8_t> payload = item.payload.span();
  EXPECT_EQ(Bytes(payload.begin(), payload.end()),
            (Bytes{0xFC, 0x80, 0x80, 0xFD, 0x81, 0x82}));
  const std::span<const uint8_t> sample_data =
      parsed.output.samples[1].data.span();
  EXPECT_EQ(Bytes(sample_data.begin(), sample_data.end()), sample.Data());
}

TEST(Mp4DemuxerCaptionTest, CaptionExtractionCanBeDisabled) {
  Writer sample;
  sample.U32(4).U8(0x65).U8(1).U8(2).U8(3);
  ProgressiveSpec spec;
  TrackSpec video = AvcTrack(kVideoId, 1, 8);
  video.data_override[0] = sample.Data();
  video.samples[0].size = 8;
  spec.tracks = {video};
  Mp4DemuxerOptions options;
  options.extract_captions = false;
  const Parsed parsed(BuildProgressive(spec), 4096, options);
  ASSERT_TRUE(parsed.result.status.ok());
  EXPECT_TRUE(parsed.output.items.empty());
}

}
}
