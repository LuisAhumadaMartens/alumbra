#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../source/fake_data_source.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/shared_bytes.h"
#include "test_support.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/source/buffered_reader.h"

namespace umbra::demux::test {
namespace {

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
    for (int guard = 0; guard < 100000; ++guard) {
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

  uint64_t BytesRequested() const {
    uint64_t total = 0;
    for (const source::test::ReadLogEntry& entry : source_->Log()) {
      total += entry.size;
    }
    return total;
  }

 private:
  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<source::test::FakeDataSource> source_;
  std::unique_ptr<source::BufferedReader> reader_;
};

TEST(Mp4SourceTest, MoovAtTheEndOpensWithAFewRangeRequests) {
  ProgressiveSpec spec;
  spec.moov_at_end = true;
  TrackSpec video;
  video.id = 1;
  video.timescale = 30000;
  video.codec = TestCodec::kAvc;
  video.samples = std::vector<SampleSpec>(30, MakeSample(40000, 1001, 0, true));
  TrackSpec audio;
  audio.id = 2;
  audio.timescale = 44100;
  audio.samples = std::vector<SampleSpec>(60, MakeSample(1500, 1024, 0, true));
  spec.tracks = {video, audio};
  const BuiltFile built = BuildProgressive(spec);
  const Bytes content = built.Flatten();
  ASSERT_GT(content.size(), 1200000U);

  SourcePump pump(content);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 1).ok());
  ASSERT_FALSE(output.samples.empty());
  EXPECT_EQ(output.streams.size(), 2U);
  EXPECT_LE(pump.RangeRequests(), 3U);
  EXPECT_LT(pump.BytesRequested(), content.size());
}

TEST(Mp4SourceTest, ReadsAWholeFragmentedFileThroughTheBufferedReader) {
  FragmentedSpec spec;
  TrackSpec audio;
  audio.id = 2;
  audio.timescale = 44100;
  audio.samples = std::vector<SampleSpec>(16, MakeSample(300, 1024, 0, true));
  spec.tracks = {audio};
  spec.samples_per_fragment = 4;
  spec.fragments = 4;
  const FragmentedFile file = BuildFragmented(spec);
  SourcePump pump(file.bytes);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  ASSERT_TRUE(pump.Run(demuxer, output, 0).ok());
  EXPECT_EQ(output.samples.size(), 16U);
  EXPECT_EQ(pump.RangeRequests(), 1U);
}

}
}
