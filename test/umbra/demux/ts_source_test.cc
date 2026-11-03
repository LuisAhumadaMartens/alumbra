#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../source/fake_data_source.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "test_support.h"
#include "ts_builder.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_demuxer.h"
#include "umbra/source/buffered_reader.h"

namespace umbra::demux::test {
namespace {

constexpr uint16_t kVideoPid = 0x101;
constexpr uint16_t kAudioPid = 0x102;

class SourcePump {
 public:
  explicit SourcePump(Bytes content)
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        source_(std::make_shared<source::test::FakeDataSource>(
            std::move(content), true, true, runner_)) {
    source::BufferedReaderOptions options;
    options.read_ahead_bytes = source::kMinReadAheadBytes;
    options.chunk_bytes = 16384;
    auto reader = source::BufferedReader::Create(source_, runner_, options);
    reader_ = *std::move(reader);
  }

  absl::StatusOr<penumbra::SharedBytes> Fetch(uint64_t offset, size_t size) {
    absl::StatusOr<penumbra::SharedBytes> result =
        absl::InternalError("pending");
    bool done = false;
    const bool posted = runner_->PostTask([&] {
      reader_->Read(offset, size, [&](absl::StatusOr<penumbra::SharedBytes> r) {
        result = std::move(r);
        done = true;
      });
    });
    EXPECT_TRUE(posted);
    environment_.RunUntilIdle();
    for (int guard = 0; guard < 1000 && !done; ++guard) {
      source_->DeliverAll();
      environment_.RunUntilIdle();
    }
    EXPECT_TRUE(done);
    return result;
  }

  absl::Status Run(Demuxer& demuxer, const RecordingOutput& output,
                   size_t stop_after_samples) {
    demuxer.SetSourceInfo(source_->Info().size, source_->Info().seekable);
    for (int guard = 0; guard < 200000; ++guard) {
      const absl::StatusOr<StepResult> step = demuxer.Step();
      if (!step.ok()) {
        return step.status();
      }
      if (step->kind == StepKind::kEndOfStream ||
          (stop_after_samples > 0 &&
           output.samples.size() >= stop_after_samples)) {
        return absl::OkStatus();
      }
      if (step->kind != StepKind::kNeedMoreData) {
        continue;
      }
      const uint64_t offset = demuxer.NextOffset();
      absl::StatusOr<penumbra::SharedBytes> bytes = Fetch(offset, 65536);
      if (!bytes.ok()) {
        return bytes.status();
      }
      if (bytes->empty()) {
        demuxer.EndOfInput();
        continue;
      }
      const absl::Status appended = demuxer.Append(offset, *std::move(bytes));
      if (!appended.ok()) {
        return appended;
      }
    }
    return absl::InternalError("the demuxer did not finish");
  }

  size_t RangeRequests() const {
    size_t ranges = 0;
    uint64_t next = UINT64_MAX;
    for (const source::test::ReadLogEntry& entry : source_->Log()) {
      if (entry.offset != next) {
        ++ranges;
      }
      next = entry.offset + entry.size;
    }
    return ranges;
  }

 private:
  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<source::test::FakeDataSource> source_;
  std::unique_ptr<source::BufferedReader> reader_;
};

SyntheticSpec LongSpec() {
  SyntheticSpec spec;
  spec.frames = 360;
  spec.gop = 25;
  spec.video_bytes = 19000;
  spec.captions = false;
  return spec;
}

int64_t LastPts(const SyntheticSpec& spec) {
  return static_cast<int64_t>(spec.start) +
         (static_cast<int64_t>(spec.frames - 1) * kFrameTicks) +
         kVideoDelayTicks;
}

TEST(TsSourceTest, ALongFileOpensWithItsDurationFromTheTail) {
  const SyntheticSpec spec = LongSpec();
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  ASSERT_GT(content.size(), 6U * 1024 * 1024);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 1).ok());
  ASSERT_EQ(output.streams.size(), 2U);
  ASSERT_NE(output.seek_map, nullptr);
  EXPECT_TRUE(output.seek_map->IsSeekable());
  const int64_t ticks = LastPts(spec) - static_cast<int64_t>(spec.start);
  EXPECT_EQ(output.seek_map->Duration().InMicroseconds(),
            ticks * 1000000 / 90000);
  EXPECT_LE(pump.RangeRequests(), 4U);
  EXPECT_EQ(output.seek_map_count, 1U);
  EXPECT_EQ(output.stream_announcements, 1U);
  ASSERT_FALSE(output.samples.empty());
  EXPECT_FALSE(output.samples.front().discontinuity);
  EXPECT_EQ(output.samples.front().dts.value, static_cast<int64_t>(spec.start));
}

TEST(TsSourceTest, SeekingNeedsALogarithmicNumberOfRangeRequests) {
  const SyntheticSpec spec = LongSpec();
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 1).ok());
  const size_t opening = pump.RangeRequests();
  const int64_t target_ticks = 1728000;
  const std::optional<SeekPoint> point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMicroseconds(target_ticks * 1000000 / 90000));
  ASSERT_TRUE(point.has_value());
  output.samples.clear();
  const absl::StatusOr<uint64_t> offset = demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok()) << offset.status();
  EXPECT_EQ(demuxer.NextOffset(), *offset);
  ASSERT_TRUE(pump.Run(demuxer, output, 40).ok());
  const size_t probes = pump.RangeRequests() - opening;
  const double bound = std::ceil(
      std::log2(static_cast<double>(content.size()) / kTsSeekGranularity));
  EXPECT_LE(static_cast<double>(probes), bound + 3);
  EXPECT_GE(probes, 2U);
  const auto video = output.SamplesOf(kVideoPid);
  ASSERT_FALSE(video.empty());
  EXPECT_TRUE(video.front().key_frame);
  EXPECT_TRUE(video.front().discontinuity);
  EXPECT_LE(video.front().pts.value, target_ticks);
  EXPECT_GE(video.front().pts.value, target_ticks - (int64_t{3} * 90000));
  const auto audio = output.SamplesOf(kAudioPid);
  ASSERT_FALSE(audio.empty());
  EXPECT_TRUE(audio.front().discontinuity);
}

TEST(TsSourceTest, ASeekToTheStartReturnsTheFirstPacket) {
  const SyntheticSpec spec = LongSpec();
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 30).ok());
  const std::optional<SeekPoint> point =
      output.seek_map->SeekPointFor(penumbra::MediaTime::Zero());
  ASSERT_TRUE(point.has_value());
  output.samples.clear();
  const absl::StatusOr<uint64_t> offset = demuxer.Seek(*point);
  ASSERT_TRUE(offset.ok());
  EXPECT_EQ(*offset, 0U);
  ASSERT_TRUE(pump.Run(demuxer, output, 5).ok());
  const auto video = output.SamplesOf(kVideoPid);
  ASSERT_FALSE(video.empty());
  EXPECT_EQ(video.front().dts.value, static_cast<int64_t>(spec.start));
  EXPECT_TRUE(video.front().discontinuity);
}

TEST(TsSourceTest, ASeekPastTheEndLandsOnTheLastKeyFrame) {
  const SyntheticSpec spec = LongSpec();
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 1).ok());
  const std::optional<SeekPoint> point =
      output.seek_map->SeekPointFor(penumbra::MediaTime::FromSeconds(100000));
  ASSERT_TRUE(point.has_value());
  output.samples.clear();
  ASSERT_TRUE(demuxer.Seek(*point).ok());
  ASSERT_TRUE(pump.Run(demuxer, output, 0).ok());
  const auto video = output.SamplesOf(kVideoPid);
  ASSERT_FALSE(video.empty());
  EXPECT_TRUE(video.front().key_frame);
  EXPECT_EQ(video.front().dts.value,
            static_cast<int64_t>(spec.start) + (350 * kFrameTicks));
  EXPECT_EQ(video.back().dts.value,
            static_cast<int64_t>(spec.start) + (359 * kFrameTicks));
}

TEST(TsSourceTest, SeekingBackAndForthKeepsFindingKeyFramesBeforeTheTarget) {
  const SyntheticSpec spec = LongSpec();
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 1).ok());
  for (const int64_t seconds : {20, 12, 23, 15, 11}) {
    const std::optional<SeekPoint> point = output.seek_map->SeekPointFor(
        penumbra::MediaTime::FromSeconds(seconds));
    ASSERT_TRUE(point.has_value());
    output.samples.clear();
    ASSERT_TRUE(demuxer.Seek(*point).ok());
    ASSERT_TRUE(pump.Run(demuxer, output, 60).ok()) << seconds;
    const auto video = output.SamplesOf(kVideoPid);
    ASSERT_FALSE(video.empty()) << seconds;
    EXPECT_TRUE(video.front().key_frame) << seconds;
    EXPECT_LE(video.front().pts.value, seconds * 90000) << seconds;
    EXPECT_GE(video.front().pts.value, (seconds * 90000) - (int64_t{3} * 90000))
        << seconds;
  }
}

TEST(TsSourceTest, ASourceWithoutASizeCannotSeek) {
  SyntheticSpec spec;
  spec.frames = 40;
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  demuxer.SetSourceInfo(std::nullopt, false);
  ASSERT_TRUE(demuxer.Append(0, penumbra::SharedBytes::Copy(content)).ok());
  demuxer.EndOfInput();
  for (int guard = 0; guard < 1000; ++guard) {
    const absl::StatusOr<StepResult> step = demuxer.Step();
    ASSERT_TRUE(step.ok());
    if (step->kind == StepKind::kEndOfStream) {
      break;
    }
  }
  EXPECT_EQ(output.SamplesOf(kVideoPid).size(), 40U);
  ASSERT_NE(output.seek_map, nullptr);
  EXPECT_FALSE(output.seek_map->IsSeekable());
  EXPECT_TRUE(output.seek_map->Duration().InMicroseconds() ==
              penumbra::TimeDelta::Unknown().InMicroseconds());
  EXPECT_FALSE(
      output.seek_map->SeekPointFor(penumbra::MediaTime::FromSeconds(1))
          .has_value());
  const absl::StatusOr<uint64_t> result = demuxer.Seek(
      SeekPoint{.time = penumbra::MediaTime::FromSeconds(1), .byte_offset = 0});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(TsSourceTest, AShortFileGetsItsSeekMapAtTheEndAndCanBeSeeked) {
  SyntheticSpec spec;
  spec.frames = 100;
  spec.video_bytes = 4000;
  const Bytes content = Flatten(BuildSynthetic(spec).packets);
  SourcePump pump(content);
  RecordingOutput output;
  TsDemuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 0).ok());
  ASSERT_NE(output.seek_map, nullptr);
  ASSERT_TRUE(output.seek_map->IsSeekable());
  const std::optional<SeekPoint> point = output.seek_map->SeekPointFor(
      penumbra::MediaTime::FromMilliseconds(10000 + 2500));
  ASSERT_TRUE(point.has_value());
  output.samples.clear();
  ASSERT_TRUE(demuxer.Seek(*point).ok());
  ASSERT_TRUE(pump.Run(demuxer, output, 0).ok());
  const auto video = output.SamplesOf(kVideoPid);
  ASSERT_FALSE(video.empty());
  EXPECT_TRUE(video.front().key_frame);
  EXPECT_LE(video.front().pts.value, 900000 + 225000);
  EXPECT_EQ(video.back().dts.value, 900000 + (99 * kFrameTicks));
}

}
}
