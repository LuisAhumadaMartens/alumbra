#include "umbra/player/player.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "player_fixture.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/text_track.h"

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

TEST_F(PlayerTest, DefaultBufferBudgetsMatchBufferSettings) {
  const PlayerOptions options;
  EXPECT_EQ(options.video_max_bytes, 134217728U);
  EXPECT_EQ(options.audio_max_bytes, 16777216U);
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

TEST_F(PlayerTest, PublicCreateBuildsAPlayerWhoseLoadsAreUnsupported) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  player->set_src("https://example.test/a.mp4");
  ASSERT_TRUE(player->error().has_value());
  EXPECT_EQ(player->error()->code(), MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(player->CanPlayType("video/mp4"), CanPlayTypeResult::kMaybe);
}

}
}
