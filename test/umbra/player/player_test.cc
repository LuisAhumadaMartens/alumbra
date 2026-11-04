#include "umbra/player/player.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "player_fixture.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"
#include "umbra/observability/metrics.h"
#include "umbra/observability/trace.h"
#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player_access.h"
#include "umbra/player/statistics.h"
#include "umbra/player/text_track.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

#ifdef ALUMBRA_PLAYER_DESKTOP_E2E
#include "penumbra/desktop/desktop_port.h"
#endif

namespace umbra::player {
namespace {

using penumbra::PlayerState;
using test::PlayerFixture;

class PlayerTest : public PlayerFixture {};

TEST_F(PlayerTest, InitialStateMatchesTheHtmlDefaults) {
  Create();
  ASSERT_NE(player_, nullptr);
  EXPECT_EQ(player_->src(), "");
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveNothing);
  EXPECT_TRUE(player_->paused());
  EXPECT_FALSE(player_->ended());
  EXPECT_FALSE(player_->seeking());
  EXPECT_FALSE(player_->autoplay());
  EXPECT_FALSE(player_->loop());
  EXPECT_EQ(player_->preload(), Preload::kMetadata);
  EXPECT_EQ(player_->playback_rate(), 1.0);
  EXPECT_EQ(player_->default_playback_rate(), 1.0);
  EXPECT_TRUE(player_->preserves_pitch());
  EXPECT_EQ(player_->volume(), 1.0);
  EXPECT_FALSE(player_->muted());
  EXPECT_EQ(player_->buffered().length(), 0U);
  EXPECT_EQ(player_->seekable().length(), 0U);
  EXPECT_FALSE(player_->error().has_value());
  double time = -1.0;
  Do([&] { time = player_->current_time(); });
  EXPECT_EQ(time, 0.0);
}

TEST_F(PlayerTest, StatisticsAreIndependentAcrossPlayerInstances) {
  Create();
  settings::Store preview_settings(settings::GetRegistry(),
                                   settings::Platform::kLinux);
  std::unique_ptr<Player> preview;
  Do([&] { preview = CreateWithSettings(preview_settings); });
  ASSERT_NE(preview, nullptr);

  Do([&] {
    internal::PlayerAccess::ReportMetric(*player_,
                                         observability::PlaybackStart{400});
    internal::PlayerAccess::ReportMetric(*preview,
                                         observability::PlaybackStart{900});
  });

  StatisticsSnapshot main_snapshot;
  StatisticsSnapshot preview_snapshot;
  Do([&] {
    main_snapshot = player_->Statistics();
    preview_snapshot = preview->Statistics();
  });
  ASSERT_TRUE(main_snapshot.average_initial_startup_time_s.value);
  ASSERT_TRUE(preview_snapshot.average_initial_startup_time_s.value);
  EXPECT_DOUBLE_EQ(*main_snapshot.average_initial_startup_time_s.value, 0.4);
  EXPECT_DOUBLE_EQ(*preview_snapshot.average_initial_startup_time_s.value, 0.9);
  Do([&] { preview.reset(); });
}

TEST_F(PlayerTest, QoeSettingDisablesOnlyTheOwningPlayersStatistics) {
  Create();
  settings::Store preview_settings(settings::GetRegistry(),
                                   settings::Platform::kLinux);
  std::unique_ptr<Player> preview;
  Do([&] { preview = CreateWithSettings(preview_settings); });
  ASSERT_NE(preview, nullptr);
  ASSERT_TRUE(
      preview_settings.Set(settings::Layer::kUser, settings::kQoeEnabled, false)
          .ok());

  Do([&] {
    internal::PlayerAccess::ReportMetric(*player_,
                                         observability::PlaybackStart{500});
    internal::PlayerAccess::ReportMetric(*preview,
                                         observability::PlaybackStart{700});
  });

  StatisticsSnapshot main_snapshot;
  StatisticsSnapshot preview_snapshot;
  Do([&] {
    main_snapshot = player_->Statistics();
    preview_snapshot = preview->Statistics();
  });
  ASSERT_TRUE(main_snapshot.average_initial_startup_time_s.value);
  EXPECT_DOUBLE_EQ(*main_snapshot.average_initial_startup_time_s.value, 0.5);
  EXPECT_FALSE(preview_snapshot.average_initial_startup_time_s.value);
  Do([&] { preview.reset(); });
}

TEST_F(PlayerTest, AddTextTrackAddsAnAppGeneratedTrack) {
  Create();
  auto track =
      player_->AddTextTrack(TextTrackKind::kSubtitles, "English", "en");
  ASSERT_NE(track, nullptr);
  EXPECT_EQ(track->label(), "English");
  EXPECT_EQ(track->language(), "en");
  EXPECT_EQ(player_->text_tracks().length(), 1U);
  EXPECT_EQ(player_->text_tracks().item(0), track);
}

TEST_F(PlayerTest, AddTextTrackFromUrlLoadsAndParsesItsPayload) {
  Create();
  auto track = player_->AddTextTrackFromUrl(
      "https://example.test/subtitle.data", "English", "en");
  ASSERT_NE(track, nullptr);
  env_.RunUntilIdle();
  ASSERT_EQ(track->cues().size(), 1U);
  EXPECT_EQ(track->cues().front().runs.front().text, "A subtitle");
  EXPECT_EQ(player_->text_tracks().item(0), track);
}

TEST_F(PlayerTest, DurationIsNanBeforeMetadata) {
  Create();
  EXPECT_TRUE(std::isnan(player_->duration()));
  SetSrc("https://example.test/a.mp4");
  EXPECT_TRUE(std::isnan(player_->duration()));
  Open();
  EXPECT_EQ(player_->duration(), 3.0);
}

TEST_F(PlayerTest, DurationIsInfinityWhenTheStreamIsUnbounded) {
  Create();
  SetSrc("https://example.test/live.m3u8");
  source_->description.duration = penumbra::MediaTime::Infinite();
  Open();
  EXPECT_TRUE(std::isinf(player_->duration()));
  EXPECT_GT(player_->duration(), 0.0);
  EXPECT_EQ(player_->seekable().length(), 0U);
}

TEST_F(PlayerTest, SettingSrcRunsTheLoadAlgorithm) {
  Create();
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(player_->src(), "https://example.test/a.mp4");
  EXPECT_EQ(resolved_,
            (std::vector<std::string>{"https://example.test/a.mp4"}));
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
  Do([this] { player_->Load(); });
  EXPECT_EQ(resolved_.size(), 2U);
}

TEST_F(PlayerTest, LoadWithoutSrcSelectsNoResource) {
  Create();
  Do([this] { player_->Load(); });
  EXPECT_TRUE(resolved_.empty());
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
  EXPECT_FALSE(player_->error().has_value());
}

TEST_F(PlayerTest, AnEmptySrcFailsAsUnsupported) {
  Create();
  SetSrc("");
  EXPECT_TRUE(resolved_.empty());
  ASSERT_TRUE(player_->error().has_value());
  EXPECT_EQ(player_->error()->code(), MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
}

TEST_F(PlayerTest, LoadClearsTheError) {
  Create();
  SetSrc("");
  ASSERT_TRUE(player_->error().has_value());
  SetSrc("https://example.test/a.mp4");
  EXPECT_FALSE(player_->error().has_value());
}

TEST_F(PlayerTest, CurrentTimeSetBeforeMetadataIsTheDefaultStartPosition) {
  Create();
  SetSrc("https://example.test/a.mp4");
  double time = -1.0;
  Do([&] { EXPECT_TRUE(player_->set_current_time(1.5)); });
  Do([&] { time = player_->current_time(); });
  EXPECT_EQ(time, 1.5);
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveNothing);
  Open();
  EXPECT_TRUE(player_->seeking());
  Do([&] { time = player_->current_time(); });
  EXPECT_EQ(time, 1.5);
}

TEST_F(PlayerTest, CurrentTimeRejectsNonFiniteValues) {
  Create();
  Do([this] {
    EXPECT_FALSE(player_->set_current_time(std::nan("")));
    EXPECT_FALSE(
        player_->set_current_time(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(player_->FastSeek(std::nan("")));
  });
}

TEST_F(PlayerTest, SeekingUpdatesPositionAndSeekingAtOnce) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll();
  double time = 0.0;
  Do([&] { EXPECT_TRUE(player_->set_current_time(2.0)); });
  EXPECT_TRUE(player_->seeking());
  Do([&] { time = player_->current_time(); });
  EXPECT_EQ(time, 2.0);
}

TEST_F(PlayerTest, FastSeekSeeksLikeTheCurrentTimeSetter) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll();
  double time = 0.0;
  Do([&] { EXPECT_TRUE(player_->FastSeek(1.0)); });
  EXPECT_TRUE(player_->seeking());
  Do([&] { time = player_->current_time(); });
  EXPECT_EQ(time, 1.0);
}

TEST_F(PlayerTest, FastSeekUsesTheConfiguredKeyframeSnapPolicy) {
  options_.fast_seek_snap = FastSeekSnap::kNearest;
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll();
  source_->keyframe_interval_ms = 1000;
  Do([&] { EXPECT_TRUE(player_->FastSeek(1.6)); });
  EXPECT_EQ(Fake()->Seeks().back(), penumbra::MediaTime::FromSeconds(2));
}

TEST_F(PlayerTest, BufferedFollowsTheDataWrittenToThePort) {
  Create();
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(player_->buffered().length(), 0U);
  Open();
  EXPECT_EQ(player_->buffered().length(), 0U);
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  const TimeRanges ranges = player_->buffered();
  ASSERT_EQ(ranges.length(), 1U);
  EXPECT_EQ(ranges.start(0), 0.0);
  EXPECT_NEAR(ranges.end(0), 0.3, 1e-9);
  EXPECT_TRUE(std::isnan(ranges.start(1)));
}

TEST_F(PlayerTest, BufferedStartsAtTheSeekTarget) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll();
  Do([&] { EXPECT_TRUE(player_->set_current_time(1.0)); });
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  const TimeRanges ranges = player_->buffered();
  ASSERT_EQ(ranges.length(), 1U);
  EXPECT_EQ(ranges.start(0), 1.0);
  EXPECT_NEAR(ranges.end(0), 1.3, 1e-9);
}

TEST_F(PlayerTest, SeekableCoversTheKnownDuration) {
  Create();
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(player_->seekable().length(), 0U);
  Open();
  const TimeRanges ranges = player_->seekable();
  ASSERT_EQ(ranges.length(), 1U);
  EXPECT_EQ(ranges.start(0), 0.0);
  EXPECT_EQ(ranges.end(0), 3.0);
}

TEST_F(PlayerTest, PlayFailsWithNotSupportedWhenTheSourceCannotBeOpened) {
  Create();
  SetSrc("");
  StartPlayback();
  EXPECT_TRUE(results_.empty());
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kNotSupported);
  ASSERT_TRUE(results_[0].detail.has_value());
  EXPECT_EQ(results_[0].detail->code, base::ErrorCode::kCodecUnsupported);
}

TEST_F(PlayerTest, PlayFailsWithNotSupportedAfterAnOpenFailure) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Do([this] {
    source_->FailOpen(test::DetailFor(base::ErrorCode::kNetworkRequestFailed));
  });
  StartPlayback();
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kNotSupported);
  ASSERT_TRUE(results_[0].detail.has_value());
  EXPECT_EQ(results_[0].detail->code, base::ErrorCode::kNetworkRequestFailed);
}

TEST_F(PlayerTest, PlayCallbackRunsOnceFromDispatchEvents) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll();
  StartPlayback();
  EXPECT_FALSE(player_->paused());
  EXPECT_TRUE(results_.empty());
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kOk);
  EXPECT_FALSE(results_[0].detail.has_value());
  Dispatch();
  EXPECT_EQ(results_.size(), 1U);
}

TEST_F(PlayerTest, PauseAbortsAPendingPlay) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  StartPlayback();
  Do([this] { player_->Pause(); });
  EXPECT_TRUE(player_->paused());
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kAbort);
}

TEST_F(PlayerTest, LoadAbortsAPendingPlay) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  StartPlayback();
  Do([this] { player_->Load(); });
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kAbort);
}

TEST_F(PlayerTest, PlayIsRefusedWhenPlatformPolicyForbidsIt) {
  allow_playback_ = false;
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  StartPlayback();
  EXPECT_TRUE(player_->paused());
  Dispatch();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].code, PlayResultCode::kNotAllowed);
}

TEST_F(PlayerTest, PlayCallbackIsDestroyedWithoutRunningWhenThePlayerGoes) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  StartPlayback();
  Destroy();
  EXPECT_TRUE(results_.empty());
}

TEST_F(PlayerTest, DestroyingThePlayerFromACallbackDropsTheRestOfTheQueue) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Open();
  int calls = 0;
  Do([&] {
    player_->Play([&](const PlayResult&) {
      ++calls;
      player_.reset();
    });
    player_->Play([&](const PlayResult&) { ++calls; });
  });
  Preroll();
  Do([&] {
    if (player_ != nullptr) {
      player_->DispatchEvents();
    }
  });
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(player_, nullptr);
}

TEST_F(PlayerTest, EventsAvailableHookRunsWhenTheQueueBecomesNonEmpty) {
  Create();
  EXPECT_EQ(notifications_, 0U);
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(notifications_, 1U);
  Open();
  EXPECT_EQ(notifications_, 1U);
  Dispatch();
  Preroll();
  EXPECT_EQ(notifications_, 2U);
}

TEST_F(PlayerTest, PlaybackRateAcceptsNonNegativeFiniteValues) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Do([this] {
    EXPECT_TRUE(player_->set_playback_rate(2.0));
    EXPECT_FALSE(player_->set_playback_rate(-1.0));
    EXPECT_FALSE(player_->set_playback_rate(std::nan("")));
  });
  EXPECT_EQ(player_->playback_rate(), 2.0);
}

TEST_F(PlayerTest, DefaultPlaybackRateAppliesOnTheNextLoad) {
  Create();
  Do([this] {
    EXPECT_TRUE(player_->set_default_playback_rate(0.5));
    EXPECT_FALSE(player_->set_default_playback_rate(-0.5));
  });
  EXPECT_EQ(player_->default_playback_rate(), 0.5);
  EXPECT_EQ(player_->playback_rate(), 1.0);
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(player_->playback_rate(), 0.5);
}

TEST_F(PlayerTest, VolumeIsLimitedToTheUnitInterval) {
  Create();
  Do([this] {
    EXPECT_TRUE(player_->set_volume(0.25));
    EXPECT_FALSE(player_->set_volume(1.5));
    EXPECT_FALSE(player_->set_volume(-0.1));
    EXPECT_FALSE(player_->set_volume(std::nan("")));
  });
  EXPECT_EQ(player_->volume(), 0.25);
}

TEST_F(PlayerTest, MutedKeepsTheVolume) {
  Create();
  Do([this] {
    player_->set_muted(true);
    EXPECT_TRUE(player_->set_volume(0.5));
  });
  EXPECT_TRUE(player_->muted());
  EXPECT_EQ(player_->volume(), 0.5);
  Do([this] { player_->set_muted(false); });
  EXPECT_FALSE(player_->muted());
}

TEST_F(PlayerTest, FlagsAndHintsRoundTrip) {
  Create();
  Do([this] {
    player_->set_autoplay(true);
    player_->set_loop(true);
    player_->set_preload(Preload::kAuto);
    player_->set_preserves_pitch(false);
  });
  EXPECT_TRUE(player_->autoplay());
  EXPECT_TRUE(player_->loop());
  EXPECT_EQ(player_->preload(), Preload::kAuto);
  EXPECT_FALSE(player_->preserves_pitch());
}

TEST_F(PlayerTest, AutoplayStartsPlaybackWhenEnoughDataIsBuffered) {
  Create();
  Do([this] { player_->set_autoplay(true); });
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll(4);
  EXPECT_FALSE(player_->paused());
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveEnoughData);
}

TEST_F(PlayerTest, ReadyStateRisesWithTheBufferedData) {
  Create();
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveNothing);
  Open();
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveMetadata);
  Preroll();
  EXPECT_GE(player_->ready_state(), ReadyState::kHaveFutureData);
}

TEST_F(PlayerTest, IntegratorSettingChangesBufferingBehaviour) {
  Create();
  ASSERT_NE(player_, nullptr);
  Do([this] {
    EXPECT_FALSE(player_->settings()
                     .Set("buffer.start_threshold_ms", std::int64_t{10000})
                     .has_value());
  });
  SetSrc("https://example.test/a.mp4");
  Open();
  Preroll(2);
  EXPECT_LT(player_->ready_state(), ReadyState::kHaveFutureData);
}

TEST_F(PlayerTest, SettingsRejectUnknownWrongTypeAndOutOfRangeValues) {
  Create();
  ASSERT_NE(player_, nullptr);
  Do([this] {
    EXPECT_TRUE(player_->settings().Set("missing.key", true).has_value());
    EXPECT_TRUE(player_->settings()
                    .Set("buffer.start_threshold_ms", std::string("invalid"))
                    .has_value());
    EXPECT_TRUE(player_->settings()
                    .Set("buffer.start_threshold_ms", std::int64_t{-1})
                    .has_value());
  });
}

TEST_F(PlayerTest, EndedIsTrueAfterPlaybackReachesTheEnd) {
  Create();
  SetSrc("https://example.test/a.mp4", 600);
  Open();
  Preroll();
  StartPlayback();
  PortState(PlayerState::kPresenting);
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  EXPECT_TRUE(player_->ended());
  EXPECT_TRUE(player_->paused());
}

TEST_F(PlayerTest, ErrorCarriesCodeMessageAndDetail) {
  Create();
  SetSrc("https://example.test/a.mp4");
  Do([this] {
    source_->FailOpen(test::DetailFor(base::ErrorCode::kNetworkRequestFailed));
  });
  const std::optional<MediaError> error = player_->error();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(error->message().rfind("ALUMBRA-1003", 0), 0U);
  EXPECT_EQ(error->detail().code, base::ErrorCode::kNetworkRequestFailed);
  EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
}

TEST_F(PlayerTest, CreateRejectsAnInvalidPrerollAndAMissingRunner) {
  options_.preroll_ms = 20000;
  Create();
  EXPECT_EQ(player_, nullptr);
  const PlayerOptions options;
  EXPECT_EQ(Player::Create(options), nullptr);
}

TEST_F(PlayerTest, CreateRejectsInvalidAudibleRateBounds) {
  options_.minimum_audible_rate = 4.5;
  Create();
  EXPECT_EQ(player_, nullptr);
  options_.minimum_audible_rate = 2.0;
  options_.maximum_audible_rate = 1.0;
  Create();
  EXPECT_EQ(player_, nullptr);
}

TEST_F(PlayerTest, PublicCreateBuildsAPlayerThatResolvesSourceProviders) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  const std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  player->set_src("https://example.test/a.mp4");
  EXPECT_FALSE(player->error().has_value());
  EXPECT_EQ(player->network_state(), NetworkState::kLoading);
  EXPECT_EQ(player->CanPlayType("video/mp4"), CanPlayTypeResult::kMaybe);
}

TEST_F(PlayerTest, PublicCreateAppliesLogLevelAndTraceCategories) {
  std::vector<std::function<void()>> posted;
  std::vector<LogRecord> logs;
  std::vector<TraceRecord> traces;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  options.log_sink = [&logs](const LogRecord& record) {
    logs.push_back(record);
  };
  options.trace_sink = [&traces](const TraceRecord& record) {
    traces.push_back(record);
  };
  const std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  ASSERT_FALSE(player->settings()
                   .Set("observability.trace.categories",
                        std::vector<std::string>{"player"})
                   .has_value());
  while (!posted.empty()) {
    std::function<void()> task = std::move(posted.front());
    posted.erase(posted.begin());
    task();
  }
  player->set_src("https://example.test/a.mp4");
  EXPECT_FALSE(logs.empty());
  EXPECT_FALSE(traces.empty());
  EXPECT_EQ(traces.front().category, TraceCategory::kPlayer);
}

TEST_F(PlayerTest, ObservabilitySinksArePerPlayer) {
  std::vector<std::function<void()>> posted;
  std::vector<LogRecord> first_logs;
  PlayerOptions first_options;
  first_options.post_task = [&](std::function<void()> task,
                                std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  first_options.log_sink = [&first_logs](const LogRecord& record) {
    first_logs.push_back(record);
  };
  const std::unique_ptr<Player> first =
      Player::Create(std::move(first_options));
  ASSERT_NE(first, nullptr);
  PlayerOptions second_options;
  second_options.post_task = [](std::function<void()>,
                                std::chrono::microseconds) {};
  const std::unique_ptr<Player> second =
      Player::Create(std::move(second_options));
  ASSERT_NE(second, nullptr);
  first->set_src("https://example.test/first.mp4");
  const std::size_t first_log_count = first_logs.size();
  second->set_src("https://example.test/second.mp4");
  EXPECT_GT(first_log_count, 0U);
  EXPECT_EQ(first_logs.size(), first_log_count);
}

#ifdef ALUMBRA_PLAYER_DESKTOP_E2E
class DrainingAudioSink final : public penumbra::AudioSink {
 public:
  DrainingAudioSink() = default;
  DrainingAudioSink(const DrainingAudioSink&) = delete;
  DrainingAudioSink& operator=(const DrainingAudioSink&) = delete;

  ~DrainingAudioSink() override { static_cast<void>(Stop()); }

  absl::Status Start(penumbra::AudioInfo format,
                     std::shared_ptr<penumbra::AudioSource> audio) override {
    static_cast<void>(format);
    if (audio == nullptr || worker_.joinable()) {
      return absl::InvalidArgumentError("invalid test audio source");
    }
    audio_ = std::move(audio);
    stopping_.store(false);
    worker_ = std::thread([this] {
      std::vector<uint8_t> buffer(8192);
      while (!stopping_.load()) {
        if (audio_->Read(buffer) == 0) {
          std::this_thread::yield();
        }
      }
    });
    return absl::OkStatus();
  }

  absl::StatusOr<penumbra::TimeDelta> GetOutputLatency() const override {
    return penumbra::TimeDelta::Zero();
  }

  absl::Status Stop() override {
    stopping_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
    audio_.reset();
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<penumbra::AudioSource> audio_;
  std::atomic<bool> stopping_ = false;
  std::thread worker_;
};

class DrainingVideoSink final : public penumbra::VideoSink {
 public:
  DrainingVideoSink() = default;
  DrainingVideoSink(const DrainingVideoSink&) = delete;
  DrainingVideoSink& operator=(const DrainingVideoSink&) = delete;

  ~DrainingVideoSink() override { static_cast<void>(Stop()); }

  absl::Status Start(
      std::shared_ptr<penumbra::VideoFrameSource> frames) override {
    if (frames == nullptr || worker_.joinable()) {
      return absl::InvalidArgumentError("invalid test video source");
    }
    frames_ = std::move(frames);
    stopping_.store(false);
    worker_ = std::thread([this] {
      const penumbra::MediaTime latest = penumbra::MediaTime::FromMicroseconds(
          std::numeric_limits<int64_t>::max() - 1);
      while (!stopping_.load()) {
        if (!frames_->TakeDueFrame(latest).has_value()) {
          std::this_thread::yield();
        }
      }
    });
    return absl::OkStatus();
  }

  absl::Status SetBounds(penumbra::OutputBounds bounds) override {
    if (bounds.width < 0 || bounds.height < 0) {
      return absl::InvalidArgumentError("invalid test video bounds");
    }
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    stopping_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
    frames_.reset();
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<penumbra::VideoFrameSource> frames_;
  std::atomic<bool> stopping_ = false;
  std::thread worker_;
};

class DefaultAudioSinkFactoryReset final {
 public:
  DefaultAudioSinkFactoryReset() = default;
  DefaultAudioSinkFactoryReset(const DefaultAudioSinkFactoryReset&) = delete;
  DefaultAudioSinkFactoryReset& operator=(const DefaultAudioSinkFactoryReset&) =
      delete;

  ~DefaultAudioSinkFactoryReset() {
    penumbra::desktop::SetDefaultAudioSinkFactoryForTesting({});
    penumbra::desktop::SetDefaultVideoSinkFactoryForTesting({});
  }
};

class DefaultDesktopPlayerE2ETest
    : public ::testing::TestWithParam<const char*> {
 protected:
  static void PlayFixtureToEnd(const char* fixture_name, double playback_rate,
                               bool preserves_pitch = true) {
    umbra::base::TestTaskEnvironment environment;
    const auto runner = environment.CreateSequencedTaskRunner(
        umbra::base::TaskPriority::kControl);
    PlayerOptions options;
    options.headless = true;
    options.preroll_ms = 100;
    options.post_task = [runner](std::function<void()> task,
                                 std::chrono::microseconds delay) {
      static_cast<void>(runner->PostDelayedTask(
          std::move(task), absl::Microseconds(delay.count())));
    };
    options.runs_on_sequence = [runner] {
      return runner->RunsTasksInCurrentSequence();
    };
    const DefaultAudioSinkFactoryReset reset_audio_sink;
    penumbra::desktop::SetDefaultAudioSinkFactoryForTesting([] {
      return absl::StatusOr<std::unique_ptr<penumbra::AudioSink>>(
          std::make_unique<DrainingAudioSink>());
    });
    penumbra::desktop::SetDefaultVideoSinkFactoryForTesting(
        [](penumbra::OutputMode) {
          return absl::StatusOr<std::unique_ptr<penumbra::VideoSink>>(
              std::make_unique<DrainingVideoSink>());
        });
    const auto run_on_sequence = [&environment,
                                  &runner](std::function<void()> task) {
      EXPECT_TRUE(runner->PostTask(std::move(task)));
      environment.RunUntilIdle();
    };
    std::unique_ptr<Player> player;
    run_on_sequence([&player, options = std::move(options)]() mutable {
      player = Player::Create(std::move(options));
    });
    ASSERT_NE(player, nullptr);
    run_on_sequence([&] {
      ASSERT_FALSE(player->settings()
                       .Set("buffer.start_threshold_ms", std::int64_t{100})
                       .has_value());
      ASSERT_FALSE(player->settings()
                       .Set("buffer.rebuffer_threshold_ms", std::int64_t{100})
                       .has_value());
    });

    bool ended_event = false;
    double ended_position = -1.0;
    Subscription ended;
    const std::filesystem::path path =
        std::filesystem::path(ALUMBRA_PROGRESSIVE_FIXTURE_DIR) / fixture_name;
    const std::string url =
        "file://" + std::filesystem::absolute(path).string();
    run_on_sequence([&] {
      ended = player->AddEventListener<EndedEvent>(
          [&ended_event, &ended_position](const EndedEvent& event) {
            ended_event = true;
            ended_position = event.state.current_time;
          });
      player->set_default_playback_rate(playback_rate);
      player->set_src(url);
      player->set_preserves_pitch(preserves_pitch);
      player->Play([](const PlayResult&) {});
      player->set_playback_rate(playback_rate);
    });

    for (size_t index = 0; index < 100000 && !ended_event; ++index) {
      environment.FastForwardBy(absl::Milliseconds(10));
      run_on_sequence([&player] { player->DispatchEvents(); });
      std::this_thread::yield();
    }

    double duration = 0.0;
    double final_position = 0.0;
    bool is_ended = false;
    bool is_paused = false;
    ReadyState ready_state = ReadyState::kHaveNothing;
    size_t buffered_ranges = 0;
    bool has_error = false;
    std::string error_message;
    run_on_sequence([&] {
      is_ended = player->ended();
      is_paused = player->paused();
      ready_state = player->ready_state();
      buffered_ranges = player->buffered().length();
      duration = player->duration();
      final_position = player->current_time();
      if (const auto error = player->error()) {
        has_error = true;
        error_message = error->message();
      }
      ended = Subscription();
      player.reset();
    });
    EXPECT_FALSE(has_error) << error_message;
    EXPECT_TRUE(ended_event) << "ready=" << static_cast<int>(ready_state)
                             << " buffered=" << buffered_ranges;
    EXPECT_TRUE(is_ended);
    EXPECT_TRUE(is_paused);
    EXPECT_GE(duration, 0.0);
    EXPECT_GE(final_position, 0.0);
    EXPECT_NEAR(final_position, ended_position, 0.05);
    if (duration > 0.0) {
      EXPECT_NEAR(ended_position, duration, 0.05);
    }
  }
};

TEST_P(DefaultDesktopPlayerE2ETest, PlaysFixtureThroughProgressiveSourceToEnd) {
  PlayFixtureToEnd(GetParam(), 16.0);
}

TEST_F(DefaultDesktopPlayerE2ETest, PlaybackRatesReachTheHeadlessDesktopPort) {
  for (const double rate : {0.5, 1.5, 2.0}) {
    PlayFixtureToEnd("packed_mp3.mp3", rate);
  }
  PlayFixtureToEnd("packed_mp3.mp3", 1.5, false);
}

INSTANTIATE_TEST_SUITE_P(Formats, DefaultDesktopPlayerE2ETest,
                         ::testing::Values("progressive_faststart.mp4",
                                           "webm_vp8_vorbis.webm",
                                           "h264_aac.ts", "packed_mp3.mp3"));
#endif

}
}
