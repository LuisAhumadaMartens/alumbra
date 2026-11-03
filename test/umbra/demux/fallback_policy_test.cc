#include "umbra/demux/fallback_policy.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "test_support.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::demux::test {
namespace {

using settings::DemuxFfmpegMode;

class RecordingSink final : public observability::Sink {
 public:
  void Write(const observability::LogRecord& record) override {
    const std::scoped_lock lock(mutex_);
    records_.push_back(record);
  }
  [[nodiscard]] std::vector<observability::LogRecord> Records() const {
    const std::scoped_lock lock(mutex_);
    return records_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<observability::LogRecord> records_;
};

class FallbackPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sink_ = std::make_shared<RecordingSink>();
    observability::SetSink(sink_);
  }
  void TearDown() override { observability::SetSink(nullptr); }

  std::shared_ptr<RecordingSink> sink_;
};

ParserFacts Facts(bool own, bool slot, bool progressive = true) {
  return {
      .has_own_parser = own, .slot_accepts = slot, .progressive = progressive};
}

TEST_F(FallbackPolicyTest, FallbackModeUsesAnOwnParserWhenOneExists) {
  const FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(true, true)), ParserChoice::kOwn);
}

TEST_F(FallbackPolicyTest, FallbackModeUsesTheSlotForAContainerWithoutOne) {
  const FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(false, true)), ParserChoice::kFallback);
}

TEST_F(FallbackPolicyTest, ASlotThatDoesNotAcceptTheFileLeavesItUnsupported) {
  const FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(false, false)),
            ParserChoice::kUnsupported);
}

TEST_F(FallbackPolicyTest, PreferModeUsesTheSlotForProgressiveSources) {
  const FallbackPolicy policy(DemuxFfmpegMode::kPrefer, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(true, true)), ParserChoice::kFallback);
}

TEST_F(FallbackPolicyTest, PreferModeKeepsAdaptiveSourcesOnTheOwnParser) {
  const FallbackPolicy policy(DemuxFfmpegMode::kPrefer, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(true, true, false)), ParserChoice::kOwn);
}

TEST_F(FallbackPolicyTest,
       PreferModeFallsBackToTheOwnParserWhenTheSlotRejects) {
  FallbackPolicy policy(DemuxFfmpegMode::kPrefer, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(true, false)), ParserChoice::kOwn);
  EXPECT_EQ(policy.ChooseAfterSlotRejection(Facts(true, true)),
            ParserChoice::kOwn);
  EXPECT_EQ(policy.Stats().slot_rejections, 1U);
}

TEST_F(FallbackPolicyTest, SlotRejectionWithoutAnOwnParserIsUnsupported) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseAfterSlotRejection(Facts(false, true)),
            ParserChoice::kUnsupported);
}

TEST_F(FallbackPolicyTest, OffModeNeverUsesTheSlot) {
  FallbackPolicy policy(DemuxFfmpegMode::kOff, true);
  EXPECT_EQ(policy.ChooseFirst(Facts(false, true)), ParserChoice::kUnsupported);
  EXPECT_EQ(policy.ChooseFirst(Facts(true, true)), ParserChoice::kOwn);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::DataLossError("bad"), false,
                                           "mp4", Facts(true, true)),
            ParserChoice::kUnsupported);
  EXPECT_EQ(policy.Stats().retries, 0U);
}

TEST_F(FallbackPolicyTest, APortWithoutTheSlotBehavesAsOff) {
  for (const DemuxFfmpegMode mode :
       {DemuxFfmpegMode::kFallback, DemuxFfmpegMode::kPrefer,
        DemuxFfmpegMode::kOff}) {
    FallbackPolicy policy(mode, false);
    EXPECT_EQ(policy.ChooseFirst(Facts(false, true)),
              ParserChoice::kUnsupported);
    EXPECT_EQ(policy.ChooseFirst(Facts(true, true)), ParserChoice::kOwn);
    EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::DataLossError("bad"), false,
                                             "mp4", Facts(true, true)),
              ParserChoice::kUnsupported);
    EXPECT_EQ(policy.Stats().retries, 0U);
  }
}

TEST_F(FallbackPolicyTest, ARejectionBeforeTheFirstSampleRetriesOnceAndCounts) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  const absl::Status rejection = absl::DataLossError("truncated moov");
  EXPECT_EQ(policy.ChooseAfterOwnRejection(rejection, false, "mp4",
                                           Facts(true, true)),
            ParserChoice::kFallback);
  EXPECT_EQ(policy.Stats().retries, 1U);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(rejection, false, "mp4",
                                           Facts(true, true)),
            ParserChoice::kUnsupported);
  EXPECT_EQ(policy.Stats().retries, 1U);
}

TEST_F(FallbackPolicyTest, ARejectionAfterTheFirstSampleNeverSwitches) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::DataLossError("late"), true,
                                           "mp4", Facts(true, true)),
            ParserChoice::kUnsupported);
  EXPECT_EQ(policy.Stats().retries, 0U);
  EXPECT_TRUE(sink_->Records().empty());
}

TEST_F(FallbackPolicyTest, ARetryNeedsTheSlotToAcceptTheFile) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::DataLossError("bad"), false,
                                           "mp4", Facts(true, false)),
            ParserChoice::kUnsupported);
  EXPECT_EQ(policy.Stats().retries, 0U);
}

TEST_F(FallbackPolicyTest, AnOkStatusIsNotARejection) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::OkStatus(), false, "mp4",
                                           Facts(true, true)),
            ParserChoice::kUnsupported);
}

TEST_F(FallbackPolicyTest, ARetryIsLoggedAtWarningInTheDemuxCategory) {
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(policy.ChooseAfterOwnRejection(absl::DataLossError("truncated"),
                                           false, "mp4", Facts(true, true)),
            ParserChoice::kFallback);
  const auto records = sink_->Records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records[0].category, observability::Category::kDemux);
  EXPECT_EQ(records[0].level, observability::Level::kWarning);
  ASSERT_EQ(records[0].fields.size(), 2U);
  EXPECT_EQ(records[0].fields[0].name, "container");
  EXPECT_EQ(std::get<std::string>(records[0].fields[0].value), "mp4");
  EXPECT_EQ(records[0].fields[1].name, "status");
  EXPECT_NE(std::get<std::string>(records[0].fields[1].value).find("truncated"),
            std::string::npos);
}

TEST_F(FallbackPolicyTest,
       TheMp4DemuxerRejectionBeforeASampleTriggersTheRetry) {
  ProgressiveSpec spec;
  TrackSpec unsupported;
  unsupported.id = 7;
  unsupported.timescale = 44100;
  unsupported.codec = TestCodec::kUnknown;
  for (uint32_t index = 0; index < 3; ++index) {
    SampleSpec sample;
    sample.size = 20;
    sample.duration = 1024;
    unsupported.samples.push_back(sample);
  }
  spec.tracks = {unsupported};
  const BuiltFile file = BuildProgressive(spec);
  RecordingOutput output;
  Mp4Demuxer demuxer(output);
  const DriveResult result = Drive(demuxer, file, 1024);
  ASSERT_FALSE(result.status.ok());
  FallbackPolicy policy(DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(
      policy.ChooseAfterOwnRejection(result.status, !output.samples.empty(),
                                     "mp4", Facts(true, true)),
      ParserChoice::kFallback);
  EXPECT_EQ(policy.Stats().retries, 1U);
}

}
}
