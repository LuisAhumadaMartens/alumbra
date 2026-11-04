#include "umbra/pipeline/progressive_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fake_data_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline {
namespace {

class RecordingSink final : public SampleSink {
 public:
  void OnOpened(SourceDescription value) override {
    description = std::move(value);
  }
  void OnOpenFailed(base::ErrorDetail value) override {
    error = std::move(value);
  }
  void OnDurationChanged(penumbra::MediaTime value) override {
    duration = value;
  }
  void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                 std::vector<penumbra::EncodedSample> values) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    samples += values.size();
  }
  void OnEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    ++eos;
  }
  void OnSourceError(base::ErrorDetail value) override {
    error = std::move(value);
  }

  std::optional<SourceDescription> description;
  std::optional<base::ErrorDetail> error;
  penumbra::MediaTime duration = penumbra::MediaTime::Unknown();
  size_t samples = 0;
  size_t eos = 0;
};

class ProgressiveSourceTest : public ::testing::Test {
 protected:
  ProgressiveSourceTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        redactor_(std::make_shared<const base::UrlRedactor>()) {}

  static std::vector<uint8_t> Fixture(std::string name) {
    const std::string path =
        std::string(ALUMBRA_PROGRESSIVE_FIXTURE_DIR) + "/" + std::move(name);
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

  void PlayFixture(std::string name, bool finite_duration,
                   bool expects_range_seek = false) {
    source_data_ = std::make_shared<source::test::FakeDataSource>(
        Fixture(std::move(name)), true, true, runner_);
    auto created = ProgressiveSource::Create(source_data_, runner_, redactor_);
    ASSERT_TRUE(created.ok());
    auto source = std::move(*created);
    auto sink = std::make_shared<RecordingSink>();
    ASSERT_TRUE(runner_->PostTask(
        [source = source.get(), sink] { source->Open(sink); }));
    Run();
    ASSERT_TRUE(sink->description.has_value());
    ASSERT_FALSE(sink->description->streams.empty());
    if (finite_duration) {
      EXPECT_TRUE(sink->description->duration.is_finite());
    }
    for (const penumbra::StreamConfig& stream : sink->description->streams) {
      ASSERT_TRUE(
          runner_->PostTask([source = source.get(), id = stream.info.track_id] {
            source->RequestSamples(1, id, 1000);
          }));
      Run();
    }
    EXPECT_GT(sink->samples, 0U);
    EXPECT_GT(sink->eos, 0U);
    EXPECT_FALSE(sink->error.has_value());
    if (expects_range_seek) {
      EXPECT_TRUE(std::ranges::any_of(
          source_data_->Log(), [](const source::test::ReadLogEntry& read) {
            return read.offset != 0;
          }));
    }
  }

  void Run() {
    for (size_t i = 0; i < 20; ++i) {
      source_data_->DeliverAll();
      environment_.RunUntilIdle();
    }
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  std::shared_ptr<source::test::FakeDataSource> source_data_;
};

TEST_F(ProgressiveSourceTest, PlaysProgressiveMp4ToEndOfStream) {
  PlayFixture("progressive_faststart.mp4", true);
}

TEST_F(ProgressiveSourceTest, ReadsTheTailRangeForAnMp4WithTrailingMoov) {
  PlayFixture("progressive_moov_end.mp4", true, true);
}

TEST_F(ProgressiveSourceTest, PlaysWebmToEndOfStream) {
  PlayFixture("webm_vp8_vorbis.webm", false);
}

TEST_F(ProgressiveSourceTest, PlaysTransportStreamToEndOfStream) {
  PlayFixture("h264_aac.ts", false);
}

TEST_F(ProgressiveSourceTest, PlaysMp3ToEndOfStream) {
  PlayFixture("packed_mp3.mp3", false);
}

}
}
