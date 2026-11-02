#include "umbra/pipeline/playback_state_machine.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "fake_port.h"
#include "fake_sample_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/playback_state.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline {
namespace {

using Names = std::vector<std::string>;
using penumbra::MediaTime;
using penumbra::PlayerState;
using S = PlaybackState;

constexpr uint32_t kVideoTrack = 7;
constexpr uint32_t kAudioTrack = 8;

penumbra::StreamConfig VideoStream() {
  penumbra::StreamConfig config;
  config.info.track_id = kVideoTrack;
  config.info.codec = penumbra::CodecId::kH264;
  config.info.codec_string = "avc1.64001f";
  config.info.time_base = penumbra::Rational::Of(1, 1000);
  penumbra::VideoInfo video;
  video.coded_width = video.display_width = 640;
  video.coded_height = video.display_height = 360;
  config.info.format = video;
  return config;
}

penumbra::StreamConfig AudioStream() {
  auto config = VideoStream();
  config.info.track_id = kAudioTrack;
  config.info.codec = penumbra::CodecId::kAac;
  config.info.format =
      penumbra::AudioInfo{.sample_rate = 48000,
                          .channel_count = 2,
                          .channel_mask = 3,
                          .sample_format = penumbra::SampleFormat::kF32};
  return config;
}

base::ErrorDetail Detail(base::ErrorCode code) {
  const auto* entry = base::FindError(code);
  return {code, entry->category, entry->severity, entry->retryable,
          {},   nullptr};
}

class MachineTest : public ::testing::Test {
 protected:
  MachineTest()
      : control_(std::make_shared<penumbra::test::RunnerAdapter>(
            env_.CreateSequencedTaskRunner(base::TaskPriority::kControl))),
        port_(control_) {
    config_.preroll_ms = 200;
    config_.on_transition = [this](const StateTransition& transition) {
      transitions_.push_back(transition);
      edges_.insert({transition.from, transition.to});
    };
    config_.allow_playback = [this] { return allow_playback_; };
  }

  void TearDown() override { Destroy(); }

  void Destroy() {
    Do([this] { machine_.reset(); });
  }

  void Do(std::function<void()> task) {
    ASSERT_TRUE(control_->PostTask(std::move(task), absl::ZeroDuration()).ok());
    env_.RunUntilIdle();
  }

  void Create() {
    Do([this] {
      machine_ =
          std::make_unique<PlaybackStateMachine>(port_, control_, config_);
    });
  }

  void Load(int64_t duration_ms = 3000) {
    source_ = std::make_shared<test::SourceControl>();
    source_->description.streams = {VideoStream(), AudioStream()};
    source_->description.duration = MediaTime::FromMilliseconds(duration_ms);
    Do([this] {
      machine_->Load(std::make_unique<test::FakeSampleSource>(source_));
    });
  }

  void Open() {
    Do([this] { source_->CompleteOpen(); });
  }

  penumbra::test::FakePlayer* Fake() { return port_.LatestPlayer(); }

  void Grant(uint32_t track, size_t count = 3) {
    Do([this, track, count] {
      ASSERT_TRUE(Fake()->RequestData(track, count).ok());
    });
  }

  void PortState(PlayerState state,
                 std::optional<uint32_t> track = std::nullopt) {
    Do([this, state, track] {
      ASSERT_TRUE(Fake()->EmitState(state, track).ok());
    });
  }

  void Feed(size_t batches, size_t count = 3) {
    for (size_t i = 0; i < batches; ++i) {
      Grant(kVideoTrack, count);
      Grant(kAudioTrack, count);
    }
  }

  void Preroll(size_t batches = 2) {
    Feed(batches);
    PortState(PlayerState::kReady);
  }

  void StartPlayback() {
    Do([this] {
      machine_->Play([this](const PlayResult& result) {
        play_results_.push_back(result.code);
      });
    });
  }

  void Pause() {
    Do([this] { machine_->Pause(); });
  }

  void Seek(int64_t ms) {
    Do([this, ms] {
      EXPECT_TRUE(machine_->Seek(MediaTime::FromMilliseconds(ms)).ok());
    });
  }

  void SetMediaTime(int64_t ms) {
    Fake()->SetMediaTime(MediaTime::FromMilliseconds(ms));
  }

  Names Pull() {
    Names names;
    pulled_.clear();
    for (auto& entry : machine_->Events().Drain()) {
      if (auto* event = std::get_if<MediaEvent>(&entry)) {
        names.emplace_back(MediaEventName(event->type));
        pulled_.push_back(*event);
      } else {
        auto& completion = std::get<PlayCompletion>(entry);
        names.push_back(std::string("play:") +
                        ResultName(completion.result.code));
        if (completion.callback) {
          completion.callback(completion.result);
        }
      }
    }
    return names;
  }

  static const char* ResultName(PlayResultCode code) {
    switch (code) {
      case PlayResultCode::kOk:
        return "ok";
      case PlayResultCode::kNotAllowed:
        return "not-allowed";
      case PlayResultCode::kNotSupported:
        return "not-supported";
      case PlayResultCode::kAbort:
        return "abort";
    }
    return "?";
  }

  const MediaEvent& PulledEvent(std::string_view name) const {
    for (const auto& event : pulled_) {
      if (MediaEventName(event.type) == name) {
        return event;
      }
    }
    ADD_FAILURE() << "no pulled event " << name;
    return pulled_.front();
  }

  Names TransitionNames() const {
    Names names;
    for (const auto& transition : transitions_) {
      names.push_back(std::string(PlaybackStateName(transition.from)) + ">" +
                      std::string(PlaybackStateName(transition.to)));
    }
    return names;
  }

  void ToReady() {
    Create();
    Load();
    Open();
    Preroll();
    Pull();
  }

  void ToPlaying() {
    ToReady();
    StartPlayback();
    PortState(PlayerState::kPresenting);
    Pull();
  }

  void Stall() {
    source_->Hold();
    SetMediaTime(600);
    Grant(kVideoTrack);
    Grant(kAudioTrack);
    PortState(PlayerState::kReady);
  }

  void ToBuffering() {
    ToPlaying();
    Stall();
  }

  void ToEnded() {
    Create();
    Load(600);
    Open();
    Preroll();
    StartPlayback();
    PortState(PlayerState::kPresenting);
    Feed(1);
  }

  void Reach(S target) {
    switch (target) {
      case S::kIdle:
        Create();
        break;
      case S::kLoading:
        Create();
        Load();
        break;
      case S::kPrerolling:
        Create();
        Load();
        Open();
        break;
      case S::kReady:
        ToReady();
        break;
      case S::kPlaying:
        ToPlaying();
        break;
      case S::kPaused:
        ToPlaying();
        Pause();
        break;
      case S::kSeeking:
        ToReady();
        Seek(500);
        break;
      case S::kBuffering:
        ToBuffering();
        break;
      case S::kEnded:
        ToEnded();
        break;
      case S::kError:
        Create();
        Load();
        Do([this] {
          source_->FailOpen(Detail(base::ErrorCode::kNetworkRequestFailed));
        });
        break;
    }
    ASSERT_EQ(machine_->GetState(), target) << PlaybackStateName(target);
  }

  base::TestTaskEnvironment env_;
  std::shared_ptr<penumbra::ControlTaskRunner> control_;
  penumbra::test::FakePort port_;
  PlaybackStateMachine::Config config_;
  std::unique_ptr<PlaybackStateMachine> machine_;
  std::shared_ptr<test::SourceControl> source_;
  std::vector<StateTransition> transitions_;
  std::set<std::pair<S, S>> edges_;
  std::vector<MediaEvent> pulled_;
  std::vector<PlayResultCode> play_results_;
  bool allow_playback_ = true;
};

TEST_F(MachineTest, LoadStartsResourceSelection) {
  Create();
  Load();
  EXPECT_EQ(Pull(), (Names{"loadstart"}));
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kLoading);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveNothing);
  EXPECT_EQ(machine_->GetState(), S::kLoading);
  EXPECT_TRUE(source_->opened);
  EXPECT_TRUE(machine_->IsPaused());
  EXPECT_TRUE(machine_->GetDuration().is_unknown());
}

TEST_F(MachineTest, MetadataFiresDurationChangeThenLoadedMetadata) {
  Create();
  Load();
  Open();
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata"}));
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveMetadata);
  EXPECT_EQ(machine_->GetState(), S::kPrerolling);
  EXPECT_EQ(machine_->GetDuration(), MediaTime::FromSeconds(3));
  EXPECT_EQ(port_.PlayersCreated(), 1);
  EXPECT_EQ(PulledEvent("loadedmetadata").state.ready,
            ReadyState::kHaveMetadata);
  EXPECT_EQ(PulledEvent("loadedmetadata").state.network,
            NetworkState::kLoading);
  EXPECT_EQ(PulledEvent("durationchange").state.ready,
            ReadyState::kHaveNothing);
  EXPECT_EQ(PulledEvent("durationchange").state.duration,
            MediaTime::FromSeconds(3));
}

TEST_F(MachineTest, PrerollReachesEnoughDataInSpecOrder) {
  Create();
  Load();
  Open();
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "loadeddata", "canplay", "canplaythrough"}));
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveEnoughData);
  EXPECT_EQ(machine_->GetState(), S::kReady);
  EXPECT_EQ(PulledEvent("canplay").state.ready, ReadyState::kHaveEnoughData);
  EXPECT_EQ(PulledEvent("canplaythrough").state.ready,
            ReadyState::kHaveEnoughData);
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0}));
}

TEST_F(MachineTest, FutureDataAloneDoesNotFireCanPlayThrough) {
  Create();
  Load();
  Open();
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "loadeddata", "canplay"}));
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveFutureData);
  Feed(1);
  EXPECT_EQ(Pull(), (Names{"canplaythrough"}));
}

TEST_F(MachineTest, NoDataBeforePortReportsReady) {
  Create();
  Load();
  Open();
  Feed(2);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveMetadata);
  PortState(PlayerState::kReady);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveEnoughData);
}

TEST_F(MachineTest, ReadyStateIsTheMinimumOverSelectedTracks) {
  Create();
  Load();
  Open();
  Grant(kVideoTrack);
  Grant(kVideoTrack);
  Grant(kVideoTrack);
  Grant(kAudioTrack, 1);
  PortState(PlayerState::kReady);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveCurrentData);
  Grant(kAudioTrack, 2);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveFutureData);
  Grant(kAudioTrack, 3);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveEnoughData);
}

TEST_F(MachineTest, ZeroPrerollNeedsOnlyThePortToBeReady) {
  config_.preroll_ms = 0;
  Create();
  Load();
  Open();
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "loadeddata", "canplay", "canplaythrough"}));
}

TEST_F(MachineTest, NeedsDataIsForwardedToTheSourceWithTheTicket) {
  Create();
  Load();
  Open();
  Grant(kVideoTrack, 2);
  ASSERT_EQ(source_->requests.size(), 1);
  EXPECT_EQ(source_->requests[0].ticket, 1);
  EXPECT_EQ(source_->requests[0].track_id, kVideoTrack);
  EXPECT_EQ(source_->requests[0].max_samples, 2);
}

TEST_F(MachineTest, UnsolicitedSamplesAreIgnored) {
  Create();
  Load();
  Open();
  Do([this] {
    source_->sink->OnSamples(2, kVideoTrack, {penumbra::EncodedSample{}});
  });
  EXPECT_TRUE(Fake()->Samples().empty());
}

TEST_F(MachineTest, PlayAfterPrerollFiresPlayThenPlayingThenResolves) {
  ToReady();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"play", "playing", "play:ok"}));
  EXPECT_EQ(play_results_, (std::vector<PlayResultCode>{PlayResultCode::kOk}));
  EXPECT_FALSE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0}));
  EXPECT_FALSE(PulledEvent("play").state.paused);
}

TEST_F(MachineTest, PlayBeforeDataWaitsThenPlaysAtFutureData) {
  Create();
  Load();
  Open();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "play", "waiting"}));
  EXPECT_EQ(machine_->GetState(), S::kPrerolling);
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadeddata", "canplay", "playing", "play:ok",
                           "canplaythrough"}));
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0}));
}

TEST_F(MachineTest, PlayWhilePlayingResolvesWithoutEvents) {
  ToPlaying();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"play:ok"}));
}

TEST_F(MachineTest, PauseFiresTimeUpdateThenPause) {
  ToPlaying();
  Pause();
  EXPECT_EQ(Pull(), (Names{"timeupdate", "pause"}));
  EXPECT_TRUE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kPaused);
  EXPECT_EQ(Fake()->Rates().back(), 0.0);
  EXPECT_TRUE(PulledEvent("pause").state.paused);
}

TEST_F(MachineTest, PauseWhilePausedFiresNothing) {
  ToReady();
  Pause();
  EXPECT_TRUE(Pull().empty());
}

TEST_F(MachineTest, PauseRejectsPendingPlayWithAbort) {
  Create();
  Load();
  Open();
  StartPlayback();
  Pull();
  Pause();
  EXPECT_EQ(Pull(), (Names{"timeupdate", "pause", "play:abort"}));
  EXPECT_EQ(play_results_,
            (std::vector<PlayResultCode>{PlayResultCode::kAbort}));
}

TEST_F(MachineTest, AutoplayStartsAtEnoughData) {
  Create();
  Do([this] { machine_->SetAutoplay(true); });
  Load();
  Open();
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "loadeddata", "canplay", "canplaythrough", "play",
                           "playing"}));
  EXPECT_FALSE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
}

TEST_F(MachineTest, PlayOrPauseDisablesAutoplay) {
  Create();
  Do([this] { machine_->SetAutoplay(true); });
  Load();
  Open();
  Pause();
  Pull();
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadeddata", "canplay", "canplaythrough"}));
  EXPECT_TRUE(machine_->IsPaused());
}

TEST_F(MachineTest, PolicyCanRefusePlayback) {
  allow_playback_ = false;
  ToReady();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"play:not-allowed"}));
  EXPECT_TRUE(machine_->IsPaused());
  EXPECT_EQ(play_results_,
            (std::vector<PlayResultCode>{PlayResultCode::kNotAllowed}));
}

TEST_F(MachineTest, PolicyBlocksAutoplay) {
  allow_playback_ = false;
  Create();
  Do([this] { machine_->SetAutoplay(true); });
  Load();
  Open();
  Preroll();
  EXPECT_TRUE(machine_->IsPaused());
}

TEST_F(MachineTest, LifecycleTransitionsFollowTheTable) {
  ToReady();
  StartPlayback();
  Pause();
  EXPECT_EQ(TransitionNames(),
            (Names{"idle>loading", "loading>prerolling", "prerolling>ready",
                   "ready>playing", "playing>paused"}));
}

TEST_F(MachineTest, SeekWhilePausedFiresSeekingTimeUpdateSeekedCanPlay) {
  ToReady();
  Seek(500);
  EXPECT_EQ(Pull(), (Names{"seeking"}));
  EXPECT_TRUE(machine_->IsSeeking());
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveMetadata);
  EXPECT_EQ(PulledEvent("seeking").state.current_time,
            MediaTime::FromMilliseconds(500));
  EXPECT_EQ(Fake()->Seeks(),
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(500)}));
  EXPECT_EQ(source_->seeks,
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(500)}));
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"timeupdate", "seeked", "canplay"}));
  EXPECT_FALSE(machine_->IsSeeking());
  EXPECT_EQ(machine_->GetState(), S::kPaused);
  EXPECT_FALSE(PulledEvent("seeked").state.seeking);
}

TEST_F(MachineTest, SeekWhilePlayingWaitsThenPlaysAgain) {
  ToPlaying();
  Seek(500);
  EXPECT_EQ(Pull(), (Names{"seeking", "timeupdate", "waiting"}));
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"timeupdate", "seeked", "canplay", "playing"}));
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0, 0.0, 1.0}));
}

TEST_F(MachineTest, SeekClampsToTheSeekableRange) {
  ToReady();
  Seek(10000);
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::FromSeconds(3));
  Seek(-5);
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::Zero());
}

TEST_F(MachineTest, SeekRejectsNonFinitePositions) {
  ToReady();
  Do([this] {
    EXPECT_EQ(machine_->Seek(MediaTime::Unknown()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(machine_->Seek(MediaTime::Infinite()).code(),
              absl::StatusCode::kInvalidArgument);
  });
  EXPECT_TRUE(Fake()->Seeks().empty());
}

TEST_F(MachineTest, SeekBeforeMetadataBecomesTheDefaultStartPosition) {
  Create();
  Load();
  Seek(300);
  EXPECT_EQ(Pull(), (Names{"loadstart"}));
  Open();
  EXPECT_EQ(Pull(), (Names{"durationchange", "loadedmetadata", "seeking"}));
  EXPECT_EQ(Fake()->Seeks(),
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(300)}));
  Do([this] {
    EXPECT_EQ(machine_->CurrentTime(), MediaTime::FromMilliseconds(300));
  });
}

TEST_F(MachineTest, SecondSeekAbortsTheFirst) {
  ToReady();
  Seek(100);
  Seek(200);
  EXPECT_EQ(Pull(), (Names{"seeking", "seeking"}));
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"timeupdate", "seeked", "canplay"}));
  EXPECT_EQ(Fake()->Ticket(), 3);
}

TEST_F(MachineTest, SeekDuringPrerollReturnsToPrerolling) {
  Create();
  Load();
  Open();
  Seek(200);
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  Feed(1, 1);
  PortState(PlayerState::kReady);
  EXPECT_EQ(machine_->GetState(), S::kPrerolling);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveCurrentData);
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata",
                           "seeking", "loadeddata", "timeupdate", "seeked"}));
}

TEST_F(MachineTest, SeekCompletingWithoutFutureDataBuffers) {
  ToReady();
  Seek(500);
  Feed(1, 1);
  PortState(PlayerState::kReady);
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveCurrentData);
}

TEST_F(MachineTest, StaleSamplesAfterASeekAreDropped) {
  ToReady();
  source_->Hold();
  Grant(kVideoTrack);
  Seek(500);
  Do([this] { source_->Release(); });
  EXPECT_TRUE(Fake()->Samples().empty());
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveMetadata);
}

TEST_F(MachineTest, ReachingTheEndWhilePlayingPausesThenEnds) {
  ToEnded();
  Pull();
  EXPECT_EQ(machine_->GetState(), S::kEnded);
  EXPECT_TRUE(machine_->IsEnded());
  EXPECT_TRUE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kIdle);
  EXPECT_EQ(Fake()->Rates().back(), 0.0);
}

TEST_F(MachineTest, EndOfPlaybackEventOrder) {
  Create();
  Load(600);
  Open();
  Preroll();
  StartPlayback();
  Pull();
  Feed(1);
  EXPECT_EQ(Pull(),
            (Names{"progress", "suspend", "timeupdate", "pause", "ended"}));
  EXPECT_TRUE(PulledEvent("ended").state.ended);
  EXPECT_EQ(PulledEvent("ended").state.current_time,
            MediaTime::FromMilliseconds(600));
  EXPECT_EQ(PulledEvent("suspend").state.network, NetworkState::kIdle);
}

TEST_F(MachineTest, EndWhilePausedOmitsPause) {
  Create();
  Load(600);
  Open();
  Preroll();
  Pull();
  Feed(1);
  EXPECT_EQ(Pull(), (Names{"progress", "suspend", "timeupdate", "ended"}));
  EXPECT_EQ(machine_->GetState(), S::kEnded);
}

TEST_F(MachineTest, PlayAfterEndSeeksToTheStart) {
  ToEnded();
  Pull();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"seeking", "play", "waiting"}));
  EXPECT_FALSE(machine_->IsEnded());
  EXPECT_FALSE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::Zero());
}

TEST_F(MachineTest, LoopRestartsInsteadOfEnding) {
  Create();
  Do([this] { machine_->SetLoop(true); });
  Load(600);
  Open();
  Preroll();
  StartPlayback();
  Pull();
  Feed(1);
  EXPECT_EQ(Pull(),
            (Names{"progress", "suspend", "seeking", "timeupdate", "waiting"}));
  EXPECT_FALSE(machine_->IsEnded());
  EXPECT_FALSE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::Zero());
}

TEST_F(MachineTest, StalledSourceBuffersAndRecoversAutomatically) {
  ToPlaying();
  Stall();
  EXPECT_EQ(Pull(), (Names{"timeupdate", "waiting"}));
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveCurrentData);
  EXPECT_FALSE(machine_->IsPaused());
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0, 0.0}));
  Do([this] { source_->Release(); });
  EXPECT_EQ(Pull(), (Names{"canplay", "playing"}));
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0, 0.0, 1.0}));
}

TEST_F(MachineTest, PausingWhileBufferingSettlesInPaused) {
  ToBuffering();
  Pull();
  Pause();
  EXPECT_EQ(Pull(), (Names{"timeupdate", "pause"}));
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
  Do([this] { source_->Release(); });
  EXPECT_EQ(machine_->GetState(), S::kPaused);
  EXPECT_EQ(Fake()->Rates().back(), 0.0);
}

TEST_F(MachineTest, SeekWhileBufferingAbandonsTheStall) {
  ToBuffering();
  Pull();
  Seek(100);
  EXPECT_EQ(machine_->GetState(), S::kSeeking);
  EXPECT_EQ(Pull(), (Names{"seeking"}));
}

TEST_F(MachineTest, EndWhileBufferingEnds) {
  ToBuffering();
  Pull();
  PortState(PlayerState::kEndOfStream);
  EXPECT_EQ(machine_->GetState(), S::kEnded);
  EXPECT_EQ(Pull(), (Names{"timeupdate", "pause", "ended"}));
}

TEST_F(MachineTest, ReadyReportedByOurOwnPauseIsNotAnUnderflow) {
  ToPlaying();
  Pause();
  StartPlayback();
  PortState(PlayerState::kReady);
  PortState(PlayerState::kPresenting);
  EXPECT_EQ(Pull(),
            (Names{"timeupdate", "pause", "play", "playing", "play:ok"}));
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
}

TEST_F(MachineTest, UnderflowWhilePausedIsNotReported) {
  ToPlaying();
  Pause();
  Pull();
  PortState(PlayerState::kReady);
  EXPECT_TRUE(Pull().empty());
}

TEST_F(MachineTest, OpenFailureBeforeMetadataIsSourceNotSupported) {
  Create();
  Load();
  Do([this] {
    source_->FailOpen(Detail(base::ErrorCode::kNetworkRequestFailed));
  });
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  ASSERT_TRUE(machine_->GetError().has_value());
  EXPECT_EQ(machine_->GetError()->code, MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kNetworkRequestFailed);
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kNoSource);
  EXPECT_EQ(machine_->GetState(), S::kError);
  EXPECT_EQ(PulledEvent("error").state.error, MediaErrorCode::kSrcNotSupported);
  EXPECT_TRUE(source_->closed);
}

TEST_F(MachineTest, PendingPlayIsRejectedWithNotSupportedAfterTheErrorEvent) {
  Create();
  Load();
  StartPlayback();
  Pull();
  Do([this] {
    source_->FailOpen(Detail(base::ErrorCode::kNetworkRequestFailed));
  });
  EXPECT_EQ(Pull(), (Names{"error", "play:not-supported"}));
  EXPECT_EQ(play_results_,
            (std::vector<PlayResultCode>{PlayResultCode::kNotSupported}));
}

TEST_F(MachineTest, PlayAfterSourceNotSupportedRejectsImmediately) {
  Reach(S::kError);
  Pull();
  StartPlayback();
  EXPECT_EQ(Pull(), (Names{"play:not-supported"}));
}

TEST_F(MachineTest, UnsupportedStreamAtPortCreationFailsTheResource) {
  Create();
  Load();
  port_.FailNextCreate(absl::UnimplementedError("codec"));
  Open();
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  EXPECT_EQ(machine_->GetError()->code, MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kCodecUnsupported);
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveNothing);
  EXPECT_EQ(port_.ActivePlayers(), 0);
}

TEST_F(MachineTest, PortExhaustionFailsWithDecoderInitialization) {
  Create();
  Load();
  port_.FailNextCreate(absl::ResourceExhaustedError("limit"));
  Open();
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kDecoderInitializationFailed);
}

TEST_F(MachineTest, SourceWithoutPlayableStreamsFails) {
  Create();
  Load();
  source_->description.streams.clear();
  Open();
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kCodecUnsupported);
  EXPECT_EQ(port_.PlayersCreated(), 0);
}

TEST_F(MachineTest, FatalPortErrorAfterMetadataSetsMediaError) {
  ToReady();
  Do([this] {
    ASSERT_TRUE(
        Fake()
            ->ReportError({.status = absl::InternalError("decoder"),
                           .category = penumbra::PortErrorCategory::kDecode,
                           .recovery = penumbra::RecoveryHint::kFatal})
            .ok());
  });
  EXPECT_EQ(Pull(), (Names{"error"}));
  EXPECT_EQ(machine_->GetError()->code, MediaErrorCode::kDecode);
  EXPECT_EQ(machine_->GetError()->detail.code, base::ErrorCode::kMediaCorrupt);
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kIdle);
  EXPECT_EQ(machine_->GetState(), S::kError);
  EXPECT_EQ(port_.ActivePlayers(), 0);
  EXPECT_TRUE(source_->closed);
}

TEST_F(MachineTest, PortErrorKeepsAnAttachedErrorDetail) {
  ToReady();
  const base::UrlRedactor redactor;
  const auto status = base::AttachErrorDetail(
      absl::PermissionDeniedError("license"),
      Detail(base::ErrorCode::kDrmLicenseDenied), redactor);
  Do([this, &status] {
    ASSERT_TRUE(
        Fake()
            ->ReportError({.status = status,
                           .category = penumbra::PortErrorCategory::kPlatform,
                           .recovery = penumbra::RecoveryHint::kFatal})
            .ok());
  });
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kDrmLicenseDenied);
}

TEST_F(MachineTest, TransientPortErrorIsOnlyTelemetry) {
  ToPlaying();
  Do([this] {
    ASSERT_TRUE(
        Fake()
            ->ReportError({.status = absl::UnavailableError("blip"),
                           .category = penumbra::PortErrorCategory::kOutput,
                           .recovery = penumbra::RecoveryHint::kTransient})
            .ok());
  });
  EXPECT_TRUE(Pull().empty());
  EXPECT_FALSE(machine_->GetError().has_value());
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
}

void ReportRecreate(penumbra::test::FakePlayer* player) {
  ASSERT_TRUE(
      player
          ->ReportError({.status = absl::UnavailableError("lost"),
                         .category = penumbra::PortErrorCategory::kPlatform,
                         .recovery = penumbra::RecoveryHint::kRecreate})
          .ok());
}

TEST_F(MachineTest, RecreateHintRebuildsThePlayerAndResumes) {
  ToPlaying();
  SetMediaTime(600);
  Do([this] { ReportRecreate(Fake()); });
  EXPECT_EQ(port_.PlayersCreated(), 2);
  EXPECT_EQ(port_.ActivePlayers(), 1);
  EXPECT_EQ(Pull(), (Names{"timeupdate", "waiting"}));
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
  EXPECT_EQ(Fake()->Seeks(),
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(600)}));
  EXPECT_EQ(source_->seeks,
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(600)}));
  Preroll(1);
  EXPECT_EQ(Pull(), (Names{"canplay", "playing"}));
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
  EXPECT_FALSE(machine_->GetError().has_value());
}

TEST_F(MachineTest, RecreateWhilePausedBuffersWithoutEvents) {
  ToPlaying();
  Pause();
  Pull();
  Do([this] { ReportRecreate(Fake()); });
  EXPECT_TRUE(Pull().empty());
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
}

TEST_F(MachineTest, RecreateWhileReadyBuffers) {
  ToReady();
  Do([this] { ReportRecreate(Fake()); });
  EXPECT_EQ(machine_->GetState(), S::kBuffering);
}

TEST_F(MachineTest, RepeatedRecreationBecomesFatal) {
  config_.max_recreate_attempts = 1;
  ToPlaying();
  Do([this] { ReportRecreate(Fake()); });
  EXPECT_FALSE(machine_->GetError().has_value());
  Do([this] { ReportRecreate(Fake()); });
  ASSERT_TRUE(machine_->GetError().has_value());
  EXPECT_EQ(machine_->GetState(), S::kError);
  EXPECT_EQ(port_.ActivePlayers(), 0);
}

TEST_F(MachineTest, PresentingResetsTheRecreationBudget) {
  config_.max_recreate_attempts = 1;
  ToPlaying();
  Do([this] { ReportRecreate(Fake()); });
  Preroll(1);
  PortState(PlayerState::kPresenting);
  Do([this] { ReportRecreate(Fake()); });
  EXPECT_FALSE(machine_->GetError().has_value());
}

TEST_F(MachineTest, FailedRecreationIsFatal) {
  ToPlaying();
  port_.FailNextCreate(absl::UnavailableError("no decoder"));
  Do([this] { ReportRecreate(Fake()); });
  ASSERT_TRUE(machine_->GetError().has_value());
  EXPECT_EQ(machine_->GetError()->detail.code,
            base::ErrorCode::kDecoderInitializationFailed);
}

TEST_F(MachineTest, PortSeekFailureRecreatesTheInstance) {
  ToReady();
  Fake()->FailNextSeek(absl::UnavailableError("seek"));
  Seek(500);
  EXPECT_EQ(port_.PlayersCreated(), 2);
  EXPECT_FALSE(machine_->GetError().has_value());
  EXPECT_EQ(Fake()->Seeks(),
            (std::vector<MediaTime>{MediaTime::FromMilliseconds(500)}));
  EXPECT_TRUE(machine_->IsSeeking());
}

TEST_F(MachineTest, FatalSourceErrorAfterMetadata) {
  ToPlaying();
  Do([this] {
    source_->sink->OnSourceError(
        Detail(base::ErrorCode::kNetworkRequestFailed));
  });
  EXPECT_EQ(Pull(), (Names{"error"}));
  EXPECT_EQ(machine_->GetError()->code, MediaErrorCode::kNetwork);
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kIdle);
}

TEST_F(MachineTest, NonFatalSourceErrorsDoNotStopPlayback) {
  ToPlaying();
  Do([this] {
    source_->sink->OnSourceError(Detail(base::ErrorCode::kNetworkTimeout));
  });
  EXPECT_TRUE(Pull().empty());
  EXPECT_EQ(machine_->GetState(), S::kPlaying);
}

TEST_F(MachineTest, SeekAfterFatalErrorIsRejected) {
  Reach(S::kError);
  Do([this] {
    EXPECT_EQ(machine_->Seek(MediaTime::Zero()).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(MachineTest, LoadWhilePendingResetsAndRejectsPlays) {
  Create();
  Load();
  Open();
  StartPlayback();
  Load();
  EXPECT_EQ(Pull(), (Names{"play:abort", "abort", "emptied", "loadstart"}));
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveNothing);
  EXPECT_TRUE(machine_->IsPaused());
  EXPECT_EQ(machine_->GetState(), S::kLoading);
  EXPECT_EQ(port_.ActivePlayers(), 0);
}

TEST_F(MachineTest, LoadFromPlayingFiresAbortEmptiedTimeUpdate) {
  ToPlaying();
  SetMediaTime(600);
  Do([this] { machine_->Play([](const PlayResult&) {}); });
  Pull();
  Do([this] {
    EXPECT_EQ(machine_->CurrentTime(), MediaTime::FromMilliseconds(600));
  });
  auto old_source = source_;
  Load();
  EXPECT_EQ(Pull(), (Names{"abort", "emptied", "timeupdate", "loadstart"}));
  EXPECT_TRUE(old_source->closed);
  EXPECT_EQ(machine_->GetDuration(), MediaTime::Unknown());
  EXPECT_TRUE(PulledEvent("emptied").state.paused);
  EXPECT_EQ(PulledEvent("emptied").state.network, NetworkState::kEmpty);
}

TEST_F(MachineTest, LoadClearsTheError) {
  Reach(S::kError);
  Pull();
  Load();
  EXPECT_FALSE(machine_->GetError().has_value());
  EXPECT_EQ(machine_->GetState(), S::kLoading);
}

TEST_F(MachineTest, LoadingNothingEmptiesTheElement) {
  ToReady();
  Do([this] { machine_->Load(nullptr); });
  EXPECT_EQ(Pull(), (Names{"abort", "emptied"}));
  EXPECT_EQ(machine_->GetNetworkState(), NetworkState::kEmpty);
  EXPECT_EQ(machine_->GetState(), S::kIdle);
}

TEST_F(MachineTest, LoadDropsUndeliveredEvents) {
  Create();
  Load();
  Open();
  Load();
  EXPECT_EQ(Pull(), (Names{"abort", "emptied", "loadstart"}));
}

TEST_F(MachineTest, OldSourceCallbacksAreIgnoredAfterReload) {
  Create();
  Load();
  auto old_source = source_;
  Load();
  Pull();
  Do([&old_source] { old_source->CompleteOpen(); });
  EXPECT_TRUE(Pull().empty());
  EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveNothing);
  EXPECT_EQ(port_.PlayersCreated(), 0);
}

TEST_F(MachineTest, LoadFromEveryStateResetsToIdleThenLoading) {
  for (const S state : kAllPlaybackStates) {
    Destroy();
    Reach(state);
    Load();
    EXPECT_EQ(machine_->GetState(), S::kLoading) << PlaybackStateName(state);
    EXPECT_EQ(machine_->GetReadyState(), ReadyState::kHaveNothing);
    EXPECT_FALSE(machine_->GetError().has_value());
  }
  for (const S state : kAllPlaybackStates) {
    if (state != S::kIdle) {
      EXPECT_TRUE(edges_.contains({state, S::kIdle}))
          << PlaybackStateName(state);
    }
  }
}

TEST_F(MachineTest, EveryLegalTransitionIsExercised) {
  const auto fail = [this] {
    Do([this] {
      source_->sink->OnSourceError(
          Detail(base::ErrorCode::kNetworkRequestFailed));
    });
  };
  const auto recreate = [this] { Do([this] { ReportRecreate(Fake()); }); };
  const auto feed_more = [this] {
    Grant(kVideoTrack);
    Grant(kAudioTrack);
  };

  Create();
  Load();
  Open();
  Seek(200);
  Feed(1, 1);
  PortState(PlayerState::kReady);
  fail();

  Reach(S::kReady);
  Seek(500);
  Feed(1, 1);
  PortState(PlayerState::kReady);
  feed_more();
  Seek(100);
  Preroll(1);
  StartPlayback();
  Seek(300);
  Preroll(1);
  Pause();
  PortState(PlayerState::kEndOfStream);
  Seek(0);
  Preroll(1);
  fail();

  Create();
  Load();
  Open();
  StartPlayback();
  Preroll(1);
  Pause();
  recreate();
  StartPlayback();
  fail();

  Reach(S::kReady);
  PortState(PlayerState::kEndOfStream);
  fail();

  Reach(S::kPaused);
  PortState(PlayerState::kEndOfStream);
  Seek(0);

  Reach(S::kPlaying);
  Seek(100);
  fail();

  Reach(S::kPlaying);
  fail();

  Reach(S::kReady);
  fail();

  Reach(S::kPrerolling);
  fail();

  Reach(S::kPaused);
  fail();

  Reach(S::kBuffering);
  Seek(100);

  Reach(S::kBuffering);
  Do([this] { source_->Release(); });

  Reach(S::kBuffering);
  Pause();
  Do([this] { source_->Release(); });

  Reach(S::kBuffering);
  PortState(PlayerState::kEndOfStream);
  fail();

  Reach(S::kBuffering);
  fail();

  Reach(S::kReady);
  recreate();

  Reach(S::kEnded);
  fail();

  Reach(S::kError);

  std::set<std::pair<S, S>> legal;
  for (const S from : kAllPlaybackStates) {
    for (const S to : kAllPlaybackStates) {
      if (IsLegalTransition(from, to) && to != S::kIdle) {
        legal.insert({from, to});
      }
    }
  }
  for (const auto& edge : legal) {
    EXPECT_TRUE(edges_.contains(edge))
        << PlaybackStateName(edge.first) << " -> "
        << PlaybackStateName(edge.second);
  }
  for (const auto& edge : edges_) {
    EXPECT_TRUE(IsLegalTransition(edge.first, edge.second));
  }
}

TEST_F(MachineTest, DestroyingInEveryStateReleasesEverything) {
  for (const S state : kAllPlaybackStates) {
    Destroy();
    Reach(state);
    auto sentinel = std::make_shared<int>(0);
    const std::weak_ptr<int> weak = sentinel;
    Do([this, sentinel = std::move(sentinel)]() mutable {
      machine_->Play([sentinel](const PlayResult&) {});
      sentinel.reset();
    });
    Do([this] { machine_.reset(); });
    EXPECT_TRUE(weak.expired()) << PlaybackStateName(state);
    EXPECT_EQ(port_.ActivePlayers(), 0) << PlaybackStateName(state);
    if (source_ != nullptr && state != S::kIdle) {
      EXPECT_TRUE(source_->closed) << PlaybackStateName(state);
    }
    env_.RunUntilIdle();
  }
}

TEST_F(MachineTest, EventsAfterShutdownAreDropped) {
  ToPlaying();
  Do([this] { machine_->Shutdown(); });
  EXPECT_EQ(port_.ActivePlayers(), 0);
  Do([this] {
    machine_->Play([](const PlayResult&) {});
    machine_->Pause();
    EXPECT_EQ(machine_->Seek(MediaTime::Zero()).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  EXPECT_TRUE(machine_->Events().empty());
}

TEST_F(MachineTest, StopFailureKeepsThePlayerUntilQuiescent) {
  ToPlaying();
  auto* player = Fake();
  player->FailNextStops(2);
  Do([this] { machine_.reset(); });
  EXPECT_EQ(port_.ActivePlayers(), 1);
  EXPECT_FALSE(player->IsStopped());
  env_.FastForwardBy(absl::Milliseconds(10));
  EXPECT_EQ(port_.ActivePlayers(), 1);
  env_.FastForwardBy(absl::Milliseconds(10));
  EXPECT_EQ(port_.ActivePlayers(), 0);
}

TEST_F(MachineTest, PortEventsAfterDestructionAreNoOps) {
  ToPlaying();
  auto* player = Fake();
  player->FailNextStops(1);
  Do([this] { machine_.reset(); });
  Do([player] {
    EXPECT_TRUE(player->EmitState(PlayerState::kReady).ok());
    EXPECT_TRUE(player->RequestData(kVideoTrack, 1).ok());
  });
  env_.FastForwardBy(absl::Milliseconds(20));
  EXPECT_EQ(port_.ActivePlayers(), 0);
}

TEST_F(MachineTest, PlaybackRateIsValidatedAndAppliedWhilePlaying) {
  ToPlaying();
  Do([this] {
    EXPECT_EQ(machine_->SetPlaybackRate(-1).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(machine_->SetPlaybackRate(std::nan("")).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(machine_->SetPlaybackRate(2.0).ok());
    EXPECT_TRUE(machine_->SetPlaybackRate(2.0).ok());
  });
  EXPECT_EQ(Pull(), (Names{"ratechange"}));
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 1.0, 2.0}));
  EXPECT_EQ(machine_->GetPlaybackRate(), 2.0);
}

TEST_F(MachineTest, PlaybackRateWhilePausedWaitsForPlay) {
  ToReady();
  Do([this] { EXPECT_TRUE(machine_->SetPlaybackRate(0.5).ok()); });
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0}));
  StartPlayback();
  EXPECT_EQ(Fake()->Rates(), (std::vector<double>{0.0, 0.5}));
}

TEST_F(MachineTest, NonFiniteRatesAreRejected) {
  ToPlaying();
  Do([this] {
    EXPECT_EQ(machine_->SetPlaybackRate(std::numeric_limits<double>::infinity())
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(machine_->GetPlaybackRate(), 1.0);
  });
}

TEST_F(MachineTest, DefaultPlaybackRateAppliesAtLoad) {
  Create();
  Do([this] {
    EXPECT_TRUE(machine_->SetDefaultPlaybackRate(1.5).ok());
    EXPECT_TRUE(machine_->SetPlaybackRate(3.0).ok());
  });
  EXPECT_EQ(Pull(), (Names{"ratechange", "ratechange"}));
  Load();
  EXPECT_EQ(Pull(), (Names{"ratechange", "loadstart"}));
  EXPECT_EQ(machine_->GetPlaybackRate(), 1.5);
}

TEST_F(MachineTest, VolumeAndMutedReachThePort) {
  ToReady();
  Do([this] {
    EXPECT_EQ(machine_->SetVolume(2.0).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(machine_->SetVolume(0.5).ok());
    EXPECT_TRUE(machine_->SetMuted(true).ok());
    EXPECT_TRUE(machine_->SetMuted(true).ok());
    EXPECT_TRUE(machine_->SetMuted(false).ok());
  });
  EXPECT_EQ(Pull(), (Names{"volumechange", "volumechange", "volumechange"}));
  EXPECT_EQ(Fake()->Volumes(), (std::vector<double>{1.0, 0.5, 0.0, 0.5}));
}

TEST_F(MachineTest, VolumeSetBeforeLoadIsAppliedToTheNewPlayer) {
  Create();
  Do([this] {
    EXPECT_TRUE(machine_->SetVolume(0.25).ok());
    EXPECT_TRUE(machine_->SetMuted(true).ok());
  });
  Load();
  Open();
  EXPECT_EQ(Fake()->Volumes(), (std::vector<double>{0.0}));
}

TEST_F(MachineTest, DurationChangesFireAndClampTheEnd) {
  ToReady();
  Do([this] { source_->sink->OnDurationChanged(MediaTime::FromSeconds(2)); });
  EXPECT_EQ(Pull(), (Names{"durationchange"}));
  EXPECT_EQ(machine_->GetDuration(), MediaTime::FromSeconds(2));
  Do([this] { source_->sink->OnDurationChanged(MediaTime::Infinite()); });
  EXPECT_EQ(Pull(), (Names{"durationchange"}));
  Seek(5000);
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::FromSeconds(5));
}

TEST_F(MachineTest, ShrinkingDurationBelowThePositionSeeksToTheEnd) {
  ToReady();
  Seek(1500);
  Preroll(1);
  Pull();
  Do([this] { source_->sink->OnDurationChanged(MediaTime::FromSeconds(1)); });
  EXPECT_EQ(Pull(), (Names{"durationchange", "seeking"}));
  EXPECT_EQ(Fake()->Seeks().back(), MediaTime::FromSeconds(1));
}

TEST_F(MachineTest, QueueNotifiesWhenFirstEventArrives) {
  Create();
  int notifications = 0;
  machine_->Events().SetNotifyHook([&notifications] { ++notifications; });
  Load();
  EXPECT_EQ(notifications, 1);
  Open();
  EXPECT_EQ(notifications, 1);
  Pull();
  Pause();
  Do([this] { machine_->Play([](const PlayResult&) {}); });
  EXPECT_EQ(notifications, 2);
}

TEST_F(MachineTest, ConfigValidationEnforcesTheSettingRange) {
  PlaybackStateMachine::Config config;
  EXPECT_TRUE(PlaybackStateMachine::ValidateConfig(config).ok());
  config.preroll_ms = 0;
  EXPECT_TRUE(PlaybackStateMachine::ValidateConfig(config).ok());
  config.preroll_ms = 10000;
  EXPECT_TRUE(PlaybackStateMachine::ValidateConfig(config).ok());
  config.preroll_ms = -1;
  EXPECT_FALSE(PlaybackStateMachine::ValidateConfig(config).ok());
  config.preroll_ms = 10001;
  EXPECT_FALSE(PlaybackStateMachine::ValidateConfig(config).ok());
  config.preroll_ms = 200;
  config.max_recreate_attempts = -1;
  EXPECT_FALSE(PlaybackStateMachine::ValidateConfig(config).ok());
}

}
}
