#include "penumbra/reference_player/decoder_chain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra/reference_player/decoder_blocklist.h"
#include "penumbra/reference_player/drop_attribution.h"
#include "penumbra_interface/fake_port.h"
#include "scripted_decoder.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace penumbra::test {
namespace {

using Names = std::vector<std::string>;

StreamConfig Video(int32_t width = 640, uint32_t track = 7) {
  StreamConfig config;
  config.info.track_id = track;
  config.info.codec = CodecId::kH264;
  config.info.codec_string = "avc1.64001f";
  config.info.time_base = Rational::Of(1, 1000000);
  VideoInfo video;
  video.coded_width = video.display_width = width;
  video.coded_height = video.display_height = width * 9 / 16;
  config.info.format = video;
  return config;
}

EncodedSample Sample(int64_t pts, bool key = false) {
  EncodedSample sample;
  sample.track_id = 7;
  sample.data = SharedBytes::Adopt({1, 2, 3});
  sample.pts = {.value = pts, .time_base = Rational::Of(1, 1000000)};
  sample.key_frame = key;
  return sample;
}

EncodedSample ChangeSample(int64_t pts, int32_t width) {
  auto sample = Sample(pts, true);
  sample.config_change = std::make_shared<StreamInfo>(Video(width).info);
  return sample;
}

DecoderCandidate Candidate(std::string name,
                           DecoderKind kind = DecoderKind::kBundledSoftware,
                           bool secure = false) {
  DecoderCandidate candidate;
  candidate.name = std::move(name);
  candidate.kind = kind;
  candidate.secure = secure;
  candidate.codecs = {CodecId::kH264};
  return candidate;
}

PortError Failure(absl::StatusCode code = absl::StatusCode::kInternal,
                  RecoveryHint hint = RecoveryHint::kRecreate) {
  return {.status = absl::Status(code, "boom"),
          .category = PortErrorCategory::kDecode,
          .recovery = hint};
}

class Sink final : public DecoderCallbacks {
 public:
  absl::Status OnDecoded(DecoderEvent event) override {
    events.push_back(std::move(event));
    return absl::OkStatus();
  }

  [[nodiscard]] std::vector<int64_t> FramePts() const {
    std::vector<int64_t> result;
    for (const auto& event : events) {
      if (const auto* frame = std::get_if<DecodedVideoFrame>(&event.detail)) {
        result.push_back(frame->presentation_time.InMicroseconds());
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<int32_t> FrameWidths() const {
    std::vector<int32_t> result;
    for (const auto& event : events) {
      if (const auto* frame = std::get_if<DecodedVideoFrame>(&event.detail)) {
        result.push_back(frame->width);
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<NeedsData> Grants() const {
    std::vector<NeedsData> result;
    for (const auto& event : events) {
      if (const auto* needs = std::get_if<NeedsData>(&event.detail)) {
        result.push_back(*needs);
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<PortError> Errors() const {
    std::vector<PortError> result;
    for (const auto& event : events) {
      if (const auto* error = std::get_if<PortError>(&event.detail)) {
        result.push_back(*error);
      }
    }
    return result;
  }

  [[nodiscard]] size_t EndsOfStream() const {
    size_t count = 0;
    for (const auto& event : events) {
      const auto* state = std::get_if<StateChanged>(&event.detail);
      if (state != nullptr && state->state == PlayerState::kEndOfStream) {
        ++count;
      }
    }
    return count;
  }

  std::vector<DecoderEvent> events;
};

class Observer final : public DecoderChainObserver {
 public:
  void OnFallback(const DecoderFallbackEvent& event) override {
    fallbacks.push_back(event);
  }
  void OnDropTrigger(const DropTriggerEvent& event) override {
    triggers.push_back(event);
  }
  std::vector<DecoderFallbackEvent> fallbacks;
  std::vector<DropTriggerEvent> triggers;
};

class ChainTest : public ::testing::Test {
 protected:
  ChainTest()
      : control_(std::make_shared<RunnerAdapter>(
            environment_.CreateSequencedTaskRunner(
                umbra::base::TaskPriority::kControl))),
        port_(control_) {}

  void Run(ControlTaskRunner::Task task) {
    ASSERT_TRUE(control_->PostTask(std::move(task), absl::ZeroDuration()).ok());
    environment_.RunUntilIdle();
  }

  DecoderChainConfig Config() {
    DecoderChainConfig config;
    config.factory = &factory_;
    config.capabilities = &port_;
    config.control = control_;
    config.downstream = sink_;
    config.observer = observer_;
    config.stream = Video();
    config.capability_request.stream = config.stream.info;
    config.settings = settings_;
    config.require_secure = require_secure_;
    config.blocklist = blocklist_;
    config.max_retained_samples = max_retained_;
    return config;
  }

  void Create(SeekTicket ticket = 1) {
    Run([this, ticket] {
      auto created = DecoderChain::Create(Config(), ticket);
      create_status_ = created.status();
      if (created.ok()) {
        chain_ = std::move(*created);
      }
    });
  }

  void Grant(const std::string& name, size_t count) {
    Run([this, name, count] {
      EXPECT_TRUE(factory_.RequestData(name, count).ok());
    });
  }

  void Write(std::vector<EncodedSample> samples, SeekTicket ticket = 1) {
    Run([this, samples = std::move(samples), ticket] {
      EXPECT_TRUE(chain_->WriteSamples(ticket, samples).ok());
    });
  }

  void Fail(const std::string& name, PortError error = Failure()) {
    Run([this, name, error = std::move(error)] {
      EXPECT_TRUE(factory_.EmitError(name, error).ok());
    });
  }

  void FlushTo(SeekTicket ticket) {
    Run([this, ticket] { EXPECT_TRUE(chain_->Flush(ticket).ok()); });
  }

  void TearDown() override {
    Run([this] {
      if (chain_ != nullptr) {
        EXPECT_TRUE(chain_->Stop().ok());
      }
      chain_.reset();
    });
  }

  umbra::base::TestTaskEnvironment environment_;
  std::shared_ptr<ControlTaskRunner> control_;
  FakePort port_;
  ScriptedFactory factory_;
  std::shared_ptr<Sink> sink_ = std::make_shared<Sink>();
  std::shared_ptr<Observer> observer_ = std::make_shared<Observer>();
  DecoderSettings settings_;
  std::span<const BlocklistEntry> blocklist_;
  bool require_secure_ = false;
  size_t max_retained_ = 512;
  absl::Status create_status_;
  std::unique_ptr<DecoderChain> chain_;
};

TEST_F(ChainTest, UsesTheFirstCandidateThatInitialises) {
  factory_.AddCandidate(Candidate("a"),
                        {.create_status = absl::UnavailableError("no device")});
  factory_.AddCandidate(Candidate("b"));
  Create();
  ASSERT_TRUE(create_status_.ok()) << create_status_;
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  EXPECT_EQ(chain_->GetStats().decoder_name, "b");
  EXPECT_EQ(chain_->GetStats().fallbacks, 1U);
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].previous, "a");
  EXPECT_EQ(observer_->fallbacks[0].next, "b");
  EXPECT_EQ(observer_->fallbacks[0].reason,
            FallbackReason::kConfigurationFailed);
  EXPECT_EQ(observer_->fallbacks[0].cause.code(),
            absl::StatusCode::kUnavailable);
}

TEST_F(ChainTest, ReportsAnErrorOnlyWhenEveryCandidateFails) {
  factory_.AddCandidate(Candidate("a"),
                        {.create_status = absl::UnavailableError("one")});
  factory_.AddCandidate(Candidate("b"),
                        {.create_status = absl::ResourceExhaustedError("two")});
  Create();
  EXPECT_EQ(create_status_.code(), absl::StatusCode::kInternal);
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  ASSERT_EQ(observer_->fallbacks.size(), 2U);
  EXPECT_EQ(observer_->fallbacks[0].previous, "a");
  EXPECT_EQ(observer_->fallbacks[0].next, "b");
  EXPECT_EQ(observer_->fallbacks[1].previous, "b");
  EXPECT_TRUE(observer_->fallbacks[1].next.empty());
}

TEST_F(ChainTest, UnsupportedCodecIsDistinctFromFailedInitialisation) {
  factory_.AddCandidate(
      Candidate("a"),
      {.create_status = absl::UnimplementedError("cannot configure")});
  Create();
  EXPECT_EQ(create_status_.code(), absl::StatusCode::kUnimplemented);
}

TEST_F(ChainTest, NoCandidateSupportingTheStreamIsUnimplemented) {
  auto other = Candidate("a");
  other.codecs = {CodecId::kAv1};
  factory_.AddCandidate(other);
  Create();
  EXPECT_EQ(create_status_.code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(factory_.CreationAttempts("a"), 0U);
}

TEST_F(ChainTest, CapabilityQueryGatesTheChain) {
  factory_.AddCandidate(Candidate("a"));
  Run([this] {
    auto config = Config();
    config.stream.info.codec = CodecId::kUnknown;
    config.capability_request.stream = config.stream.info;
    EXPECT_EQ(DecoderChain::Create(config, 1).status().code(),
              absl::StatusCode::kUnimplemented);
    config.stream = Video();
    config.capability_request.key_system = "com.widevine.alpha";
    EXPECT_EQ(DecoderChain::Create(config, 1).status().code(),
              absl::StatusCode::kUnimplemented);
  });
  EXPECT_EQ(factory_.CreationAttempts("a"), 0U);
}

TEST_F(ChainTest, DisabledFallbackNeverMovesOn) {
  settings_.fallback_enabled = false;
  factory_.AddCandidate(Candidate("a"),
                        {.create_status = absl::UnavailableError("no device")});
  factory_.AddCandidate(Candidate("b"));
  Create();
  EXPECT_FALSE(create_status_.ok());
  EXPECT_EQ(factory_.CreationAttempts("b"), 0U);
}

TEST_F(ChainTest, DisabledFallbackReportsEarlyErrorsAsFatal) {
  settings_.fallback_enabled = false;
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  factory_.AddCandidate(Candidate("b"));
  Create();
  ASSERT_TRUE(create_status_.ok());
  Grant("a", 2);
  Write({Sample(0, true)});
  Fail("a");
  ASSERT_EQ(sink_->Errors().size(), 1U);
  EXPECT_EQ(sink_->Errors()[0].recovery, RecoveryHint::kFatal);
  EXPECT_EQ(factory_.CreationAttempts("b"), 0U);
  Run([this] {
    EXPECT_FALSE(chain_->Flush(2).ok());
    EXPECT_FALSE(chain_->SwitchToNextCandidate().ok());
  });
}

TEST_F(ChainTest, ErrorBeforeFirstOutputRefeedsRetainedSamples) {
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  factory_.AddCandidate(Candidate("b"));
  Create();
  ASSERT_TRUE(create_status_.ok());
  Grant("a", 4);
  ASSERT_EQ(sink_->Grants().size(), 1U);
  Write({Sample(0, true), Sample(1000), Sample(2000)});
  EXPECT_EQ(factory_.Records("a")[0]->written_pts.size(), 3U);
  EXPECT_EQ(chain_->GetStats().retained_samples, 3U);
  Fail("a");
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  EXPECT_TRUE(factory_.Records("a")[0]->stopped);
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].reason,
            FallbackReason::kErrorBeforeFirstOutput);
  EXPECT_EQ(observer_->fallbacks[0].cause.message(), "boom");
  Grant("b", 2);
  EXPECT_EQ(factory_.Records("b")[0]->written_pts,
            (std::vector<int64_t>{0, 1000}));
  EXPECT_EQ(chain_->GetStats().retained_samples, 0U);
  Grant("b", 8);
  EXPECT_EQ(factory_.Records("b")[0]->written_pts,
            (std::vector<int64_t>{0, 1000, 2000}));
  EXPECT_EQ(sink_->FramePts(), (std::vector<int64_t>{0, 1000, 2000}));
  EXPECT_EQ(sink_->Grants().size(), 1U);
  Grant("b", 4);
  ASSERT_EQ(sink_->Grants().size(), 2U);
  EXPECT_EQ(sink_->Grants()[1].max_samples, 4U);
  EXPECT_TRUE(sink_->Errors().empty());
}

TEST_F(ChainTest, ADecoderTheChainLeftStaysExcluded) {
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  factory_.AddCandidate(Candidate("b"), {.emit_output = false});
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Fail("a");
  Grant("b", 2);
  Fail("b");
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  ASSERT_EQ(sink_->Errors().size(), 1U);
  EXPECT_EQ(sink_->Errors()[0].recovery, RecoveryHint::kFatal);
  EXPECT_EQ(sink_->Errors()[0].status.code(), absl::StatusCode::kInternal);
  ASSERT_EQ(observer_->fallbacks.size(), 2U);
  EXPECT_EQ(observer_->fallbacks[1].previous, "b");
  EXPECT_TRUE(observer_->fallbacks[1].next.empty());
  Run([this] {
    auto sample = Sample(5);
    EXPECT_FALSE(chain_->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_FALSE(chain_->Flush(2).ok());
  });
  EXPECT_EQ(factory_.CreationAttempts("a"), 1U);
}

TEST_F(ChainTest, SynchronousWriteFailureBeforeOutputFallsBack) {
  factory_.AddCandidate(
      Candidate("a"),
      {.write_status = absl::InternalError("bad write"), .fail_write_call = 1});
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true), Sample(1000)});
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  Grant("b", 4);
  EXPECT_EQ(factory_.Records("b")[0]->written_pts,
            (std::vector<int64_t>{0, 1000}));
  EXPECT_EQ(sink_->FramePts(), (std::vector<int64_t>{0, 1000}));
}

TEST_F(ChainTest, ErrorAfterFirstOutputRequestsARecreateAndReseek) {
  factory_.AddCandidate(Candidate("a"));
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  ASSERT_EQ(sink_->FramePts().size(), 1U);
  Fail("a");
  ASSERT_EQ(sink_->Errors().size(), 1U);
  EXPECT_EQ(sink_->Errors()[0].recovery, RecoveryHint::kRecreate);
  EXPECT_EQ(sink_->Errors()[0].category, PortErrorCategory::kDecode);
  EXPECT_EQ(factory_.CreationAttempts("a"), 1U);
  EXPECT_EQ(chain_->GetStats().fallbacks, 0U);
  FlushTo(2);
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "a"}));
  EXPECT_EQ(factory_.Records("a")[1]->created_ticket, 2U);
  EXPECT_TRUE(factory_.Records("a")[0]->stopped);
  EXPECT_EQ(chain_->GetStats().recreations, 1U);
  EXPECT_TRUE(observer_->fallbacks.empty());
  Grant("a", 2);
  ASSERT_EQ(sink_->Grants().size(), 2U);
  EXPECT_EQ(sink_->events.back().ticket, 2U);
}

TEST_F(ChainTest, ARecreatedDecoderFailingBeforeOutputMovesOn) {
  factory_.AddCandidate(Candidate("a"));
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Fail("a");
  factory_.SetScript("a", {.emit_output = false});
  FlushTo(2);
  Grant("a", 2);
  Write({Sample(5000, true)}, 2);
  Fail("a");
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "a", "b"}));
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].previous, "a");
  EXPECT_EQ(observer_->fallbacks[0].next, "b");
  Grant("b", 2);
  EXPECT_EQ(factory_.Records("b")[0]->written_pts,
            (std::vector<int64_t>{5000}));
}

TEST_F(ChainTest, CorruptedSamplesAfterFirstOutputAreCountedNotFallenBack) {
  factory_.AddCandidate(Candidate("a"));
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Fail("a", Failure(absl::StatusCode::kDataLoss, RecoveryHint::kTransient));
  EXPECT_EQ(chain_->GetStats().corrupted, 1U);
  EXPECT_TRUE(sink_->Errors().empty());
  EXPECT_EQ(factory_.CreationAttempts("b"), 0U);
  Grant("a", 2);
  Write({Sample(1000)});
  EXPECT_EQ(sink_->FramePts(), (std::vector<int64_t>{0, 1000}));
}

TEST_F(ChainTest, FlushDeliversNoStaleFrames) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Grant("a", 2);
  Run([this] {
    EXPECT_TRUE(factory_.EmitFrame("a", 100).ok());
    EXPECT_TRUE(factory_.EmitFrame("a", 200).ok());
    EXPECT_TRUE(chain_->Flush(2).ok());
  });
  EXPECT_TRUE(sink_->FramePts().empty());
  Grant("a", 2);
  Write({Sample(9000, true)}, 2);
  EXPECT_EQ(sink_->FramePts(), (std::vector<int64_t>{9000}));
  EXPECT_EQ(sink_->events.back().ticket, 2U);
}

TEST_F(ChainTest, FlushClearsPendingSamplesAndGrants) {
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  Create();
  Grant("a", 4);
  Run([this] {
    auto samples = std::vector{Sample(0, true), Sample(1000)};
    EXPECT_TRUE(chain_->WriteSamples(1, samples).ok());
    EXPECT_TRUE(chain_->Flush(2).ok());
  });
  EXPECT_TRUE(factory_.Records("a")[0]->written_pts.empty());
  EXPECT_EQ(chain_->GetStats().retained_samples, 0U);
  Run([this] {
    auto sample = Sample(0, true);
    EXPECT_EQ(chain_->WriteSamples(2, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(ChainTest, ResolutionChangeAppliedInPlaceKeepsTheDecoder) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Grant("a", 2);
  Write({ChangeSample(1000, 1280)});
  Grant("a", 2);
  Write({Sample(2000)});
  EXPECT_EQ(factory_.CreationAttempts("a"), 1U);
  EXPECT_EQ(sink_->FrameWidths(), (std::vector<int32_t>{640, 1280, 1280}));
}

TEST_F(ChainTest, ResolutionChangeRecreatesTheDecoderWhenNotApplicableInPlace) {
  factory_.AddCandidate(Candidate("a"), {.reject_config_change = true});
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Grant("a", 2);
  Write({ChangeSample(1000, 1280), Sample(2000)});
  EXPECT_EQ(factory_.CreationAttempts("a"), 2U);
  EXPECT_EQ(chain_->GetStats().recreations, 1U);
  EXPECT_TRUE(observer_->fallbacks.empty());
  const auto records = factory_.Records("a");
  const auto* video = std::get_if<VideoInfo>(&records[1]->config.info.format);
  ASSERT_NE(video, nullptr);
  EXPECT_EQ(video->coded_width, 1280);
  Grant("a", 4);
  EXPECT_EQ(sink_->FrameWidths(), (std::vector<int32_t>{640, 1280}));
  EXPECT_EQ(records[1]->written_pts, (std::vector<int64_t>{1000}));
  Grant("a", 4);
  EXPECT_EQ(sink_->FramePts(), (std::vector<int64_t>{0, 1000, 2000}));
  EXPECT_EQ(sink_->FrameWidths(), (std::vector<int32_t>{640, 1280, 1280}));
  EXPECT_TRUE(sink_->Errors().empty());
}

TEST_F(ChainTest, FailedRecreationForAResolutionChangeMovesOn) {
  factory_.AddCandidate(Candidate("a"), {.reject_config_change = true});
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  factory_.SetScript("a", {.create_status = absl::UnavailableError("busy"),
                           .reject_config_change = true});
  Grant("a", 2);
  Write({ChangeSample(1000, 1280)});
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "a", "b"}));
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].previous, "a");
  EXPECT_EQ(observer_->fallbacks[0].next, "b");
  EXPECT_EQ(observer_->fallbacks[0].reason,
            FallbackReason::kConfigurationFailed);
  Grant("b", 2);
  EXPECT_EQ(sink_->FrameWidths(), (std::vector<int32_t>{640, 1280}));
}

TEST_F(ChainTest, SecureStreamsNeverFallBackToNonSecureDecoders) {
  require_secure_ = true;
  factory_.AddCandidate(Candidate("secure", DecoderKind::kHardware, true),
                        {.emit_output = false});
  factory_.AddCandidate(Candidate("clear"));
  Create();
  ASSERT_TRUE(create_status_.ok());
  Grant("secure", 2);
  Write({Sample(0, true)});
  Fail("secure");
  EXPECT_EQ(factory_.CreationAttempts("clear"), 0U);
  ASSERT_EQ(sink_->Errors().size(), 1U);
  EXPECT_EQ(sink_->Errors()[0].recovery, RecoveryHint::kFatal);
}

TEST_F(ChainTest, SecureStreamWithoutASecureCandidateIsUnsupported) {
  require_secure_ = true;
  factory_.AddCandidate(Candidate("clear"));
  Create();
  EXPECT_EQ(create_status_.code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(factory_.CreationAttempts("clear"), 0U);
}

TEST_F(ChainTest, DropTriggerSwitchAppliesWithTheNextFlush) {
  factory_.AddCandidate(Candidate("a"));
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Run([this] { EXPECT_TRUE(chain_->SwitchToNextCandidate().ok()); });
  EXPECT_EQ(factory_.CreationAttempts("b"), 0U);
  FlushTo(2);
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  EXPECT_EQ(factory_.Records("b")[0]->created_ticket, 2U);
  EXPECT_EQ(chain_->GetStats().decoder_name, "b");
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].reason, FallbackReason::kDroppedFrames);
  Run([this] {
    EXPECT_EQ(chain_->SwitchToNextCandidate().code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(ChainTest, AttributedDropsAreRecordedAndTheTriggerIsReported) {
  settings_.dropped_frames_max_percent = 10;
  settings_.dropped_frames_window_ms = 1000;
  factory_.AddCandidate(Candidate("a"));
  Create();
  Run([this] {
    for (int index = 0; index < 10; ++index) {
      chain_->RecordFrameDue(TimeDelta::FromMilliseconds(int64_t{index} * 100));
    }
    chain_->RecordDroppedFrame(DropCause::kDecodeLate,
                               TimeDelta::FromMilliseconds(300));
    chain_->RecordDroppedFrame(DropCause::kDecodeLate,
                               TimeDelta::FromMilliseconds(400));
    chain_->RecordDroppedFrame(DropCause::kPresentLate,
                               TimeDelta::FromMilliseconds(500));
    chain_->PollDropTrigger(TimeDelta::FromMilliseconds(1000));
  });
  const auto stats = chain_->GetStats();
  EXPECT_EQ(stats.dropped_decode_late, 2U);
  EXPECT_EQ(stats.dropped_present_late, 1U);
  EXPECT_EQ(stats.Dropped(), 3U);
  EXPECT_EQ(stats.drop_triggers, 1U);
  ASSERT_EQ(observer_->triggers.size(), 1U);
  EXPECT_EQ(observer_->triggers[0].decode_late, 2U);
  EXPECT_EQ(observer_->triggers[0].frames_due, 10U);
  EXPECT_EQ(stats.fallbacks, 0U);
  EXPECT_EQ(factory_.CreationAttempts("a"), 1U);
}

TEST_F(ChainTest, StatisticsNameTheDecoderAndCountOutput) {
  factory_.AddCandidate(Candidate("gpu", DecoderKind::kHardware));
  Create();
  Grant("gpu", 4);
  Write({Sample(0, true), Sample(1000)});
  const auto stats = chain_->GetStats();
  EXPECT_EQ(stats.decoder_name, "gpu");
  EXPECT_EQ(stats.kind, DecoderKind::kHardware);
  EXPECT_EQ(stats.output, DecoderOutput::kCpuBuffer);
  EXPECT_EQ(stats.decoded, 2U);
}

TEST_F(ChainTest, GrantsAreEnforcedAndForwardedOnce) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Run([this] {
    auto sample = Sample(0, true);
    EXPECT_EQ(chain_->WriteSamples(1, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  Grant("a", 3);
  ASSERT_EQ(sink_->Grants().size(), 1U);
  EXPECT_EQ(sink_->Grants()[0].max_samples, 3U);
  EXPECT_EQ(sink_->Grants()[0].track_id, 7U);
  Run([this] {
    const std::array<EncodedSample, 4> too_many = {Sample(0), Sample(1),
                                                   Sample(2), Sample(3)};
    EXPECT_EQ(chain_->WriteSamples(1, too_many).code(),
              absl::StatusCode::kInvalidArgument);
    auto other_track = Sample(0);
    other_track.track_id = 9;
    EXPECT_EQ(chain_->WriteSamples(1, {&other_track, 1}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(chain_->WriteSamples(1, {}).code(),
              absl::StatusCode::kInvalidArgument);
    auto sample = Sample(0, true);
    EXPECT_EQ(chain_->WriteSamples(3, {&sample, 1}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(chain_->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_EQ(chain_->WriteSamples(1, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  EXPECT_EQ(factory_.Records("a")[0]->written_pts.size(), 1U);
  EXPECT_EQ(sink_->Grants().size(), 1U);
}

TEST_F(ChainTest, EndOfStreamIsForwardedAfterQueuedSamples) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Grant("a", 2);
  Run([this] {
    auto sample = Sample(0, true);
    EXPECT_TRUE(chain_->WriteSamples(1, {&sample, 1}).ok());
  });
  Grant("a", 2);
  Run([this] { EXPECT_TRUE(chain_->EndOfStream(1).ok()); });
  EXPECT_EQ(factory_.Records("a")[0]->end_of_streams, 1U);
  EXPECT_EQ(sink_->EndsOfStream(), 1U);
  Run([this] {
    EXPECT_EQ(chain_->EndOfStream(1).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(ChainTest, EndOfStreamSurvivesAFallback) {
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 2);
  Write({Sample(0, true)});
  Grant("a", 2);
  Run([this] { EXPECT_TRUE(chain_->EndOfStream(1).ok()); });
  Fail("a");
  Grant("b", 2);
  Grant("b", 2);
  EXPECT_EQ(factory_.Records("b")[0]->written_pts, (std::vector<int64_t>{0}));
  EXPECT_EQ(factory_.Records("b")[0]->end_of_streams, 1U);
  EXPECT_EQ(sink_->EndsOfStream(), 1U);
}

TEST_F(ChainTest, TicketsMustIncreaseAndStaleCallsAreIgnored) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Run([this] {
    EXPECT_EQ(chain_->Flush(1).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(chain_->Flush(2).ok());
    auto sample = Sample(0, true);
    EXPECT_TRUE(chain_->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_TRUE(chain_->EndOfStream(1).ok());
    EXPECT_EQ(chain_->EndOfStream(3).code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_F(ChainTest, ExceedingTheRetentionBoundForcesAReseekToFallBack) {
  max_retained_ = 2;
  factory_.AddCandidate(Candidate("a"), {.emit_output = false});
  factory_.AddCandidate(Candidate("b"));
  Create();
  Grant("a", 4);
  Write({Sample(0, true), Sample(1000)});
  Grant("a", 4);
  Write({Sample(2000)});
  EXPECT_EQ(chain_->GetStats().retained_samples, 0U);
  Fail("a");
  ASSERT_EQ(sink_->Errors().size(), 1U);
  EXPECT_EQ(sink_->Errors()[0].recovery, RecoveryHint::kRecreate);
  EXPECT_EQ(factory_.CreationAttempts("b"), 0U);
  FlushTo(2);
  EXPECT_EQ(factory_.CreationOrder(), (Names{"a", "b"}));
  ASSERT_EQ(observer_->fallbacks.size(), 1U);
  EXPECT_EQ(observer_->fallbacks[0].reason,
            FallbackReason::kErrorBeforeFirstOutput);
}

TEST_F(ChainTest, PreferredDecodersReorderTheCandidates) {
  settings_.preferred_decoders = {"software"};
  factory_.AddCandidate(Candidate("hardware", DecoderKind::kHardware));
  factory_.AddCandidate(Candidate("software"));
  Create();
  EXPECT_EQ(factory_.CreationOrder(), (Names{"software"}));
}

TEST_F(ChainTest, DefaultOrderIsHardwareThenPlatformThenBundled) {
  factory_.AddCandidate(Candidate("bundled"),
                        {.create_status = absl::UnavailableError("x")});
  factory_.AddCandidate(Candidate("platform", DecoderKind::kPlatformSoftware),
                        {.create_status = absl::UnavailableError("x")});
  factory_.AddCandidate(Candidate("hardware", DecoderKind::kHardware),
                        {.create_status = absl::UnavailableError("x")});
  Create();
  EXPECT_EQ(factory_.CreationOrder(),
            (Names{"hardware", "platform", "bundled"}));
}

TEST_F(ChainTest, BlocklistedCandidatesAreSkippedUnlessTheEntryIsIgnored) {
  static constexpr std::array kEntries = {
      BlocklistEntry{.id = "hardware-bad",
                     .decoder = "hardware",
                     .reason = "Corrupt output",
                     .evidence = "https://example.org/1",
                     .since = "0.1.0"}};
  blocklist_ = kEntries;
  factory_.AddCandidate(Candidate("hardware", DecoderKind::kHardware));
  factory_.AddCandidate(Candidate("software"));
  Create();
  EXPECT_EQ(factory_.CreationOrder(), (Names{"software"}));
  Run([this] { chain_->Stop().IgnoreError(); });
  chain_.reset();
  settings_.blocklist_ignored_entries = {"hardware-bad"};
  Create();
  EXPECT_EQ(factory_.CreationOrder(), (Names{"software", "hardware"}));
}

TEST_F(ChainTest, WorksOverThePortComponentFactory) {
  Run([this] {
    auto config = Config();
    config.factory = &port_;
    auto created = DecoderChain::Create(config, 1);
    ASSERT_TRUE(created.ok()) << created.status();
    EXPECT_EQ((*created)->GetStats().decoder_name, "fake.video");
    EXPECT_TRUE((*created)->Stop().ok());
    config.stream.info.codec = CodecId::kUnknown;
    config.capability_request.stream = config.stream.info;
    EXPECT_EQ(DecoderChain::Create(config, 1).status().code(),
              absl::StatusCode::kUnimplemented);
  });
}

TEST_F(ChainTest, StopIsIdempotentAndSilencesLaterEvents) {
  factory_.AddCandidate(Candidate("a"));
  Create();
  Grant("a", 2);
  Run([this] {
    EXPECT_TRUE(factory_.EmitFrame("a", 10).ok());
    EXPECT_TRUE(chain_->Stop().ok());
    EXPECT_TRUE(chain_->Stop().ok());
    auto sample = Sample(0, true);
    EXPECT_EQ(chain_->WriteSamples(1, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  EXPECT_TRUE(sink_->FramePts().empty());
  EXPECT_TRUE(factory_.Records("a")[0]->stopped);
}

TEST_F(ChainTest, FailedStopKeepsTheChainAlive) {
  factory_.AddCandidate(Candidate("a"),
                        {.stop_status = absl::UnavailableError("busy")});
  Create();
  Run([this] {
    EXPECT_EQ(chain_->Stop().code(), absl::StatusCode::kUnavailable);
    EXPECT_EQ(chain_->Stop().code(), absl::StatusCode::kUnavailable);
  });
  EXPECT_EQ(factory_.Records("a")[0]->stops, 2U);
  chain_.reset();
}

}
}
