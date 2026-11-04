#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "element_harness.h"
#include "gtest/gtest.h"
#include "penumbra/interface/port_types.h"
#include "player_fixture.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"

namespace umbra::player::conformance {
namespace {

using Names = std::vector<std::string>;
using penumbra::PlayerState;

using WptErrorCodesError = ElementHarness;
using WptErrorSequence = ElementHarness;
using WptSeekingSeekToCurrentTime = ElementHarness;
using WptSeekingSeekToMaxValue = ElementHarness;
using WptSeekingSeekToNegativeTime = ElementHarness;
using WptOffsetsCurrentTime = ElementHarness;
using WptOffsetsDuration = ElementHarness;
using WptPlayingPlaybackRate = ElementHarness;
using WptVolumeNonfinite = ElementHarness;
using WptVolumechangeRatechangeQueueTask = ElementHarness;
using WptLoopFromEnded = ElementHarness;
using WptAudioLoopSeekToEos = ElementHarness;
using WptReplay = ElementHarness;
using WptEventOrder = ElementHarness;
using WptEventLoadstart = ElementHarness;
using WptEventLoadedmetadata = ElementHarness;
using WptEventLoadeddata = ElementHarness;
using WptEventCanplay = ElementHarness;
using WptEventCanplaythrough = ElementHarness;
using WptEventPlaying = ElementHarness;
using WptEventPlay = ElementHarness;
using WptEventPause = ElementHarness;
using WptEventProgress = ElementHarness;
using WptEventTimeupdate = ElementHarness;
using WptEventVolumechange = ElementHarness;
using WptVolumeCheck = ElementHarness;
using WptAutoplayOverridesPreload = ElementHarness;
using WptReadyStatesAutoplay = ElementHarness;
using WptNetworkState = ElementHarness;
using WptReadyState = ElementHarness;
using WptPausedDuringEvents = ElementHarness;
using WptLoadEventsNetworkState = ElementHarness;
using WptLoadRemovesQueuedErrorEvent = ElementHarness;
using WptResourceSelectionInvoke = ElementHarness;
using WptMutedUserInterface = ElementHarness;
using WptPreloadReflection = ElementHarness;
using WptPreservesPitch = ElementHarness;
using WptMimeTypesCanPlayType = ElementHarness;
using TrackListConformance = ElementHarness;

bool Contains(const Names& names, const std::string& name) {
  return std::ranges::find(names, name) != names.end();
}

Names Only(const Names& names, const std::vector<std::string>& wanted) {
  Names kept;
  for (const std::string& name : names) {
    if (std::ranges::find(wanted, name) != wanted.end()) {
      kept.push_back(name);
    }
  }
  return kept;
}

const ObservedEvent* FindEvent(const std::vector<ObservedEvent>& events,
                               const std::string& name) {
  for (const ObservedEvent& event : events) {
    if (event.name == name) {
      return &event;
    }
  }
  return nullptr;
}

TEST_F(WptErrorCodesError, InitialValueIsNull) {
  StartElement();
  EXPECT_FALSE(player_->error().has_value());
}

TEST_F(WptErrorCodesError, NullAfterSuccessfulLoad) {
  ReachMetadata();
  Preroll();
  const Names names = Pull();
  EXPECT_TRUE(Contains(names, "loadeddata"));
  EXPECT_FALSE(Contains(names, "error"));
  EXPECT_FALSE(player_->error().has_value());
}

TEST_F(WptErrorCodesError, AfterSettingSrcToTheEmptyStringIsSrcNotSupported) {
  StartElement();
  SetSrc("");
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  const std::optional<MediaError> error = player_->error();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(static_cast<int>(error->code()), 4);
  EXPECT_EQ(error->code(), MediaErrorCode::kSrcNotSupported);
  EXPECT_FALSE(error->message().empty());
}

struct VolumeProbe {
  Player* player = nullptr;
  size_t volumechanges = 0;
  std::optional<MediaErrorCode> code_at_first;
};

TEST_F(WptErrorSequence, DISABLED_EmptySrcQueuesVolumechangeBeforeLoadstart) {
  StartElement();
  VolumeProbe probe;
  probe.player = player_.get();
  const Subscription subscription =
      player_->AddEventListener<VolumeChangeEvent>(
          [&probe](const VolumeChangeEvent&) {
            if (++probe.volumechanges == 1) {
              if (probe.player->error().has_value()) {
                probe.code_at_first = probe.player->error()->code();
              }
              static_cast<void>(probe.player->set_volume(1.0));
            }
          });
  Do([this] {
    player_->set_src("");
    EXPECT_TRUE(player_->set_volume(0.0));
  });
  Dispatch();
  Dispatch();
  EXPECT_EQ(NamesOf(log_->Take()),
            (Names{"volumechange", "loadstart", "error", "volumechange"}));
  ASSERT_TRUE(probe.code_at_first.has_value());
  EXPECT_EQ(*probe.code_at_first, MediaErrorCode::kSrcNotSupported);
}

TEST_F(WptErrorSequence, EmptySrcQueuesLoadstartThenErrorThenLaterEvents) {
  StartElement();
  Do([this] {
    player_->set_src("");
    EXPECT_TRUE(player_->set_volume(0.0));
  });
  EXPECT_EQ(Pull(), (Names{"loadstart", "error", "volumechange"}));
  ASSERT_TRUE(player_->error().has_value());
  EXPECT_EQ(player_->error()->code(), MediaErrorCode::kSrcNotSupported);
}

TEST_F(WptErrorSequence, ErrorAttributeIsSetSynchronouslyOnADecodeError) {
  ReachEnoughData();
  EXPECT_FALSE(player_->error().has_value());
  Do([this] {
    source_->sink->OnSourceError(
        test::DetailFor(base::ErrorCode::kMediaCorrupt));
  });
  ASSERT_TRUE(player_->error().has_value());
  EXPECT_EQ(player_->error()->code(), MediaErrorCode::kDecode);
  EXPECT_TRUE(Contains(Pull(), "error"));
}

TEST_F(WptErrorSequence, ADecodeErrorDoesNotRejectAPendingPlay) {
  ReachMetadata();
  PlayWithMarkers();
  EXPECT_EQ(Pull(), (Names{"play", "waiting"}));
  Do([this] {
    source_->sink->OnSourceError(
        test::DetailFor(base::ErrorCode::kMediaCorrupt));
  });
  const Names names = Pull();
  EXPECT_TRUE(Contains(names, "error"));
  EXPECT_FALSE(Contains(names, "result:rejected"));
  EXPECT_FALSE(player_->paused());
}

TEST_F(WptSeekingSeekToCurrentTime, FiresSeekingTimeupdateSeekedInOrder) {
  ReachEnoughData();
  EXPECT_GT(player_->seekable().length(), 0U);
  EXPECT_FALSE(player_->seeking());
  const double now = CurrentTime();
  Do([&] { EXPECT_TRUE(player_->set_current_time(now)); });
  EXPECT_TRUE(player_->seeking());
  CompleteSeek();
  std::vector<ObservedEvent> events = PullEvents();
  std::erase_if(events, [](const ObservedEvent& event) {
    return event.name != "seeking" && event.name != "timeupdate" &&
           event.name != "seeked";
  });
  EXPECT_EQ(NamesOf(events), (Names{"seeking", "timeupdate", "seeked"}));
  ASSERT_EQ(events.size(), 3U);
  EXPECT_FALSE(events[1].state.seeking);
  EXPECT_FALSE(events[2].state.seeking);
  EXPECT_FALSE(player_->seeking());
}

TEST_F(WptSeekingSeekToMaxValue, ClampsToTheEndOfTheSeekableRange) {
  ReachMetadata();
  ASSERT_EQ(player_->seekable().length(), 1U);
  Do([this] {
    EXPECT_TRUE(player_->set_current_time(std::numeric_limits<double>::max()));
  });
  EXPECT_TRUE(player_->seeking());
  const double end = player_->seekable().end(0);
  EXPECT_EQ(CurrentTime(), end);
  CompleteSeek();
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* seeked = FindEvent(events, "seeked");
  ASSERT_NE(seeked, nullptr);
  EXPECT_FALSE(seeked->state.seeking);
  EXPECT_EQ(seeked->state.current_time, end);
}

TEST_F(WptSeekingSeekToNegativeTime, ClampsToTheStartOfTheSeekableRange) {
  ReachMetadata();
  ASSERT_EQ(player_->seekable().length(), 1U);
  EXPECT_EQ(player_->seekable().start(0), 0.0);
  Do([this] { EXPECT_TRUE(player_->set_current_time(-1.0)); });
  EXPECT_TRUE(player_->seeking());
  EXPECT_EQ(CurrentTime(), 0.0);
  CompleteSeek();
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* seeked = FindEvent(events, "seeked");
  ASSERT_NE(seeked, nullptr);
  EXPECT_FALSE(seeked->state.seeking);
  EXPECT_EQ(seeked->state.current_time, 0.0);
}

TEST_F(WptOffsetsCurrentTime, InitialValueIsZero) {
  StartElement();
  EXPECT_EQ(CurrentTime(), 0.0);
}

TEST_F(WptOffsetsCurrentTime,
       SettingItAtHaveNothingKeepsTheValueWithoutSeeking) {
  StartElement();
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveNothing);
  Do([this] { EXPECT_TRUE(player_->set_current_time(1.0e6)); });
  EXPECT_EQ(CurrentTime(), 1.0e6);
  EXPECT_FALSE(player_->seeking());
}

TEST_F(WptOffsetsCurrentTime, DISABLED_NumberMaxValueIsKeptAtHaveNothing) {
  StartElement();
  Do([this] {
    EXPECT_TRUE(player_->set_current_time(std::numeric_limits<double>::max()));
  });
  EXPECT_EQ(CurrentTime(), std::numeric_limits<double>::max());
  EXPECT_FALSE(player_->seeking());
}

TEST_F(WptOffsetsCurrentTime, SettingItAfterMetadataStartsSeeking) {
  ReachMetadata();
  EXPECT_GT(player_->ready_state(), ReadyState::kHaveNothing);
  EXPECT_FALSE(player_->seeking());
  Do([this] { EXPECT_TRUE(player_->set_current_time(1.0)); });
  EXPECT_TRUE(player_->seeking());
}

TEST_F(WptOffsetsDuration, InitialValueIsNan) {
  StartElement();
  EXPECT_TRUE(std::isnan(player_->duration()));
}

TEST_F(WptPlayingPlaybackRate, InitialValueIsOne) {
  StartElement();
  EXPECT_EQ(player_->playback_rate(), 1.0);
}

TEST_F(WptPlayingPlaybackRate, SmallAndLargePositiveRatesFireRatechange) {
  for (const double rate : {3.0, 100.0}) {
    StartElement();
    Do([&] { EXPECT_TRUE(player_->set_playback_rate(rate)); });
    EXPECT_EQ(Pull(), (Names{"ratechange"})) << rate;
    EXPECT_EQ(player_->playback_rate(), rate);
  }
}

TEST_F(WptPlayingPlaybackRate, ZeroFiresRatechange) {
  StartElement();
  Do([this] { EXPECT_TRUE(player_->set_playback_rate(0.0)); });
  EXPECT_EQ(Pull(), (Names{"ratechange"}));
  EXPECT_EQ(player_->playback_rate(), 0.0);
}

TEST_F(WptPlayingPlaybackRate, NegativeRatesAreRejectedAndLeaveTheRateAlone) {
  StartElement();
  for (const double rate : {-3.0, -100.0, -1.0}) {
    Do([&] { EXPECT_FALSE(player_->set_playback_rate(rate)); });
    EXPECT_EQ(player_->playback_rate(), 1.0) << rate;
  }
  EXPECT_EQ(Pull(), Names{});
}

TEST_F(WptVolumeNonfinite, NonFiniteVolumesAreRejectedAndLeaveTheVolumeAlone) {
  StartElement();
  for (const double value :
       {std::nan(""), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()}) {
    Do([&] { EXPECT_FALSE(player_->set_volume(value)); });
    EXPECT_EQ(player_->volume(), 1.0);
  }
  EXPECT_EQ(Pull(), Names{});
}

TEST_F(WptVolumechangeRatechangeQueueTask,
       MutedFiresVolumechangeFromAQueuedTask) {
  StartElement();
  Do([this] { player_->set_muted(true); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
}

TEST_F(WptVolumechangeRatechangeQueueTask,
       VolumeFiresVolumechangeFromAQueuedTask) {
  StartElement();
  Do([this] { EXPECT_TRUE(player_->set_volume(0.5)); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
}

TEST_F(WptVolumechangeRatechangeQueueTask,
       PlaybackRateFiresRatechangeFromAQueuedTask) {
  StartElement();
  Do([this] { EXPECT_TRUE(player_->set_playback_rate(2.0)); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Pull(), (Names{"ratechange"}));
}

TEST_F(WptVolumechangeRatechangeQueueTask,
       DefaultPlaybackRateFiresRatechangeFromAQueuedTask) {
  StartElement();
  Do([this] { EXPECT_TRUE(player_->set_default_playback_rate(2.0)); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Pull(), (Names{"ratechange"}));
}

TEST_F(WptLoopFromEnded, PlayWithLoopSetAfterTheEndSeeksBackToTheStart) {
  ReachPlaying(600);
  FinishPlayback();
  const std::vector<ObservedEvent> ended_events = PullEvents();
  const ObservedEvent* ended = FindEvent(ended_events, "ended");
  ASSERT_NE(ended, nullptr);
  EXPECT_TRUE(ended->state.ended);
  EXPECT_TRUE(ended->state.paused);
  EXPECT_EQ(ended->state.current_time, ended->state.duration);
  EXPECT_FALSE(player_->loop());
  Do([this] { player_->set_loop(true); });
  EXPECT_TRUE(player_->loop());
  EXPECT_TRUE(player_->paused());
  PlayWithMarkers();
  CompleteSeek();
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* seeked = FindEvent(events, "seeked");
  ASSERT_NE(seeked, nullptr);
  EXPECT_FALSE(seeked->state.paused);
  EXPECT_FALSE(seeked->state.ended);
  EXPECT_LT(seeked->state.current_time, seeked->state.duration);
}

TEST_F(WptLoopFromEnded, DISABLED_SettingLoopAfterTheEndClearsEnded) {
  ReachPlaying(600);
  FinishPlayback();
  Pull();
  EXPECT_TRUE(player_->ended());
  Do([this] { player_->set_loop(true); });
  EXPECT_FALSE(player_->ended());
  EXPECT_TRUE(player_->paused());
}

TEST_F(WptAudioLoopSeekToEos, SeekingToTheEndOfALoopingElementKeepsItPlaying) {
  StartElement();
  Do([this] { player_->set_loop(true); });
  BeginLoad();
  Open();
  Preroll();
  Pull();
  PlayWithMarkers();
  PortState(PlayerState::kPresenting);
  Pull();
  Do([this] { EXPECT_TRUE(player_->set_current_time(player_->duration())); });
  CompleteSeek();
  const Names names = Pull();
  EXPECT_TRUE(Contains(names, "seeked"));
  EXPECT_TRUE(Contains(names, "timeupdate"));
  EXPECT_FALSE(player_->paused());
}

TEST_F(WptReplay, PlayingAgainAfterTheEndReachesTheEndAgain) {
  ReachPlaying(600);
  FinishPlayback();
  EXPECT_TRUE(Contains(Pull(), "ended"));
  EXPECT_TRUE(player_->ended());
  Do([this] { EXPECT_TRUE(player_->set_current_time(0.0)); });
  PlayWithMarkers();
  CompleteSeek();
  PortState(PlayerState::kPresenting);
  Pull();
  EXPECT_FALSE(player_->paused());
  FinishPlayback();
  FinishPlayback();
  EXPECT_TRUE(Contains(Pull(), "ended"));
  EXPECT_TRUE(player_->ended());
}

TEST_F(WptEventOrder, CanplayIsFiredBeforePlayingWhenAutoplayStarts) {
  StartElement();
  Do([this] { player_->set_autoplay(true); });
  BeginLoad();
  Open();
  Preroll(4);
  const Names names = Pull();
  const auto canplay = std::ranges::find(names, "canplay");
  const auto playing = std::ranges::find(names, "playing");
  ASSERT_NE(canplay, names.end());
  ASSERT_NE(playing, names.end());
  EXPECT_LT(canplay, playing);
}

TEST_F(WptEventOrder, DurationchangeThenResizeThenLoadedmetadata) {
  StartElement();
  BeginLoad();
  Pull();
  Open();
  EXPECT_EQ(Pull(), (Names{"durationchange", "resize", "loadedmetadata"}));
}

TEST_F(WptEventOrder, LoadedmetadataThenLoadeddata) {
  ReachMetadata();
  Preroll();
  const Names names = PullWithoutProgress();
  ASSERT_GE(names.size(), 1U);
  EXPECT_EQ(names.front(), "loadeddata");
}

TEST_F(WptEventOrder, CanplayThenCanplaythrough) {
  ReachMetadata();
  Preroll();
  EXPECT_EQ(PullWithoutProgress(),
            (Names{"loadeddata", "canplay", "canplaythrough"}));
}

TEST_F(WptEventOrder, LoadstartIsFirstAndProgressFollowsTheData) {
  StartElement();
  BeginLoad();
  Open();
  Grant(test::kVideoTrack);
  env_.FastForwardBy(absl::Milliseconds(350));
  const Names names = Pull();
  ASSERT_FALSE(names.empty());
  EXPECT_EQ(names.front(), "loadstart");
  EXPECT_TRUE(Contains(names, "progress"));
}

TEST_F(WptEventLoadstart, FiresAfterSettingSrcWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    EXPECT_EQ(Pull(), (Names{"loadstart"})) << autoplay;
  }
}

TEST_F(WptEventLoadedmetadata, FiresAfterSettingSrcWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    Open();
    EXPECT_TRUE(Contains(Pull(), "loadedmetadata")) << autoplay;
  }
}

TEST_F(WptEventLoadeddata, FiresAfterSettingSrcWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    Open();
    Preroll();
    EXPECT_TRUE(Contains(Pull(), "loadeddata")) << autoplay;
  }
}

TEST_F(WptEventCanplay, FiresAfterSettingSrcWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    Open();
    Preroll();
    EXPECT_TRUE(Contains(Pull(), "canplay")) << autoplay;
  }
}

TEST_F(WptEventCanplaythrough, FiresAfterSettingSrcWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    Open();
    Preroll(4);
    EXPECT_TRUE(Contains(Pull(), "canplaythrough")) << autoplay;
  }
}

TEST_F(WptEventPlaying, FiresWhenAutoplayOrPlayStartsPlayback) {
  StartElement();
  Do([this] { player_->set_autoplay(true); });
  BeginLoad();
  Open();
  Preroll(4);
  EXPECT_TRUE(Contains(Pull(), "playing"));
  ReachEnoughData();
  PlayWithMarkers();
  EXPECT_TRUE(Contains(Pull(), "playing"));
}

TEST_F(WptEventPlay, FiresWhenAutoplayOrPlayStartsPlayback) {
  StartElement();
  Do([this] { player_->set_autoplay(true); });
  BeginLoad();
  Open();
  Preroll(4);
  EXPECT_TRUE(Contains(Pull(), "play"));
  ReachEnoughData();
  PlayWithMarkers();
  EXPECT_TRUE(Contains(Pull(), "play"));
}

TEST_F(WptEventPause, FiresWhenPlaybackIsPaused) {
  ReachPlaying();
  Do([this] { player_->Pause(); });
  EXPECT_TRUE(Contains(Pull(), "pause"));
}

TEST_F(WptEventProgress, FiresWhileDataArrivesWithAndWithoutAutoplay) {
  for (const bool autoplay : {false, true}) {
    StartElement();
    Do([&] { player_->set_autoplay(autoplay); });
    BeginLoad();
    Open();
    Grant(test::kVideoTrack);
    env_.FastForwardBy(absl::Milliseconds(350));
    EXPECT_TRUE(Contains(Pull(), "progress")) << autoplay;
  }
}

TEST_F(WptEventTimeupdate, FiresWhilePlaybackProgresses) {
  ReachPlaying();
  env_.FastForwardBy(absl::Milliseconds(250));
  EXPECT_TRUE(Contains(Pull(), "timeupdate"));
}

TEST_F(WptEventVolumechange, FiresWhenVolumeOrMutedChanges) {
  StartElement();
  Do([this] {
    EXPECT_TRUE(player_->set_volume(0.5));
    player_->set_muted(true);
  });
  EXPECT_EQ(Pull(), (Names{"volumechange", "volumechange"}));
}

TEST_F(WptEventVolumechange, EventsCarryTheStateAfterEachSetter) {
  StartElement();
  Do([this] { EXPECT_TRUE(player_->set_volume(0.5)); });
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
  EXPECT_EQ(player_->volume(), 0.5);
  Do([this] { EXPECT_TRUE(player_->set_volume(1.0)); });
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
  EXPECT_EQ(player_->volume(), 1.0);
  Do([this] { player_->set_muted(true); });
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
  EXPECT_TRUE(player_->muted());
  Do([this] { player_->set_muted(false); });
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
  EXPECT_FALSE(player_->muted());
}

TEST_F(WptEventVolumechange, SettingTheSameValueDoesNotFireTheEvent) {
  StartElement();
  Do([this] {
    EXPECT_TRUE(player_->set_volume(player_->volume()));
    player_->set_muted(player_->muted());
  });
  EXPECT_EQ(Pull(), Names{});
  Do([this] { player_->set_muted(!player_->muted()); });
  EXPECT_EQ(Pull(), (Names{"volumechange"}));
}

TEST_F(WptEventVolumechange, RepeatedChangesFireRepeatedly) {
  StartElement();
  Do([this] {
    player_->set_muted(!player_->muted());
    EXPECT_TRUE(player_->set_volume(1.0 - player_->volume()));
    player_->set_muted(!player_->muted());
    EXPECT_TRUE(player_->set_volume(1.0 - player_->volume()));
  });
  EXPECT_EQ(Pull(), (Names{"volumechange", "volumechange", "volumechange",
                           "volumechange"}));
}

TEST_F(WptEventVolumechange, ChangesBeforeALoadDoNotFireTheEvent) {
  StartElement();
  Do([this] {
    player_->set_muted(!player_->muted());
    EXPECT_TRUE(player_->set_volume(1.0 - player_->volume()));
    player_->Load();
  });
  EXPECT_EQ(Pull(), Names{});
}

TEST_F(WptVolumeCheck, InitialValueIsWithinTheUnitInterval) {
  StartElement();
  EXPECT_GE(player_->volume(), 0.0);
  EXPECT_LE(player_->volume(), 1.0);
}

TEST_F(WptVolumeCheck, ValuesInsideTheRangeAreStoredAndOutsideAreRejected) {
  StartElement();
  for (const double volume : {0.5, 0.0, 1.0}) {
    Do([&] { EXPECT_TRUE(player_->set_volume(volume)); });
    EXPECT_EQ(player_->volume(), volume);
  }
  for (const double volume : {-1.1, 1.1}) {
    Do([&] { EXPECT_FALSE(player_->set_volume(volume)); });
    EXPECT_EQ(player_->volume(), 1.0);
  }
}

TEST_F(WptNetworkState, InitialValueIsEmpty) {
  StartElement();
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptNetworkState, IsLoadingDuringLoadstartAndProgress) {
  StartElement();
  BeginLoad();
  Open();
  Grant(test::kVideoTrack);
  env_.FastForwardBy(absl::Milliseconds(350));
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* loadstart = FindEvent(events, "loadstart");
  const ObservedEvent* progress = FindEvent(events, "progress");
  ASSERT_NE(loadstart, nullptr);
  ASSERT_NE(progress, nullptr);
  EXPECT_EQ(loadstart->state.network_state, NetworkState::kLoading);
  EXPECT_EQ(progress->state.network_state, NetworkState::kLoading);
}

TEST_F(WptReadyState, InitialValueIsHaveNothing) {
  StartElement();
  EXPECT_EQ(player_->ready_state(), ReadyState::kHaveNothing);
}

TEST_F(WptReadyState, MatchesTheEventThatIsFired) {
  StartElement();
  BeginLoad();
  Open();
  Preroll(4);
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* metadata = FindEvent(events, "loadedmetadata");
  const ObservedEvent* data = FindEvent(events, "loadeddata");
  const ObservedEvent* canplay = FindEvent(events, "canplay");
  const ObservedEvent* through = FindEvent(events, "canplaythrough");
  ASSERT_NE(metadata, nullptr);
  ASSERT_NE(data, nullptr);
  ASSERT_NE(canplay, nullptr);
  ASSERT_NE(through, nullptr);
  EXPECT_EQ(metadata->state.ready_state, ReadyState::kHaveMetadata);
  EXPECT_GE(data->state.ready_state, ReadyState::kHaveCurrentData);
  EXPECT_GE(canplay->state.ready_state, ReadyState::kHaveFutureData);
  EXPECT_EQ(through->state.ready_state, ReadyState::kHaveEnoughData);
}

TEST_F(WptReadyState, IsAtLeastHaveFutureDataDuringPlaying) {
  ReachEnoughData();
  PlayWithMarkers();
  const std::vector<ObservedEvent> events = PullEvents();
  const ObservedEvent* playing = FindEvent(events, "playing");
  ASSERT_NE(playing, nullptr);
  EXPECT_GE(playing->state.ready_state, ReadyState::kHaveFutureData);
}

TEST_F(WptPausedDuringEvents, IsFalseDuringPlayAndTrueDuringPause) {
  ReachEnoughData();
  PlayWithMarkers();
  const std::vector<ObservedEvent> play_events = PullEvents();
  const ObservedEvent* play = FindEvent(play_events, "play");
  ASSERT_NE(play, nullptr);
  EXPECT_FALSE(play->state.paused);
  PortState(PlayerState::kPresenting);
  Do([this] { player_->Pause(); });
  const std::vector<ObservedEvent> pause_events = PullEvents();
  const ObservedEvent* pause = FindEvent(pause_events, "pause");
  ASSERT_NE(pause, nullptr);
  EXPECT_TRUE(pause->state.paused);
}

TEST_F(WptLoadEventsNetworkState, LoadFromIdleFiresAbortThenEmptied) {
  StartElement();
  Do([this] { player_->set_preload(Preload::kNone); });
  BeginLoad();
  EXPECT_TRUE(Contains(Pull(), "suspend"));
  EXPECT_EQ(player_->network_state(), NetworkState::kIdle);
  Do([this] { player_->Load(); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Only(Pull(), {"abort", "emptied", "timeupdate", "loadstart"}),
            (Names{"abort", "emptied", "loadstart"}));
}

TEST_F(WptLoadEventsNetworkState, EmptiedIsFiredBeforeTimeupdate) {
  ReachEnoughData();
  Do([this] { EXPECT_TRUE(player_->set_current_time(1.0)); });
  Pull();
  Do([this] { player_->Load(); });
  EXPECT_TRUE(log_->Take().empty());
  const Names names = Pull();
  const auto emptied = std::ranges::find(names, "emptied");
  const auto timeupdate = std::ranges::find(names, "timeupdate");
  ASSERT_NE(emptied, names.end());
  ASSERT_NE(timeupdate, names.end());
  EXPECT_LT(emptied, timeupdate);
  EXPECT_EQ(Only(names, {"loadstart"}), Names{"loadstart"});
}

TEST_F(WptLoadEventsNetworkState, LoadWhileLoadingFiresAbortThenEmptied) {
  StartElement();
  BeginLoad();
  Pull();
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
  Do([this] { player_->Load(); });
  EXPECT_TRUE(log_->Take().empty());
  EXPECT_EQ(Pull(), (Names{"abort", "emptied", "loadstart"}));
}

TEST_F(WptLoadEventsNetworkState, LoadFromNoSourceFiresOnlyEmptied) {
  StartElement();
  SetSrc("");
  Pull();
  EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
  Do([this] { player_->Load(); });
  EXPECT_TRUE(log_->Take().empty());
  const Names names = Pull();
  ASSERT_FALSE(names.empty());
  EXPECT_EQ(names.front(), "emptied");
  EXPECT_FALSE(Contains(names, "abort"));
}

struct ReloadOnFirstLoadstart {
  Player* player = nullptr;
  bool replace_src = false;
  bool done = false;

  void operator()([[maybe_unused]] const LoadStartEvent& event) {
    if (done) {
      return;
    }
    done = true;
    if (replace_src) {
      player->set_src("https://example.test/b.mp4");
    } else {
      player->Load();
    }
  }
};

TEST_F(WptLoadRemovesQueuedErrorEvent,
       DISABLED_LoadInLoadstartRemovesTheQueuedError) {
  StartElement();
  ReloadOnFirstLoadstart reload;
  reload.player = player_.get();
  const Subscription subscription =
      player_->AddEventListener<LoadStartEvent>(std::ref(reload));
  SetSrc("");
  Dispatch();
  Dispatch();
  EXPECT_EQ(NamesOf(log_->Take()), (Names{"loadstart", "loadstart", "error"}));
}

TEST_F(WptLoadRemovesQueuedErrorEvent,
       DISABLED_ANewSrcInLoadstartLeavesNoPendingError) {
  StartElement();
  ReloadOnFirstLoadstart reload;
  reload.player = player_.get();
  reload.replace_src = true;
  const Subscription subscription =
      player_->AddEventListener<LoadStartEvent>(std::ref(reload));
  SetSrc("");
  Dispatch();
  Dispatch();
  EXPECT_EQ(NamesOf(log_->Take()), (Names{"loadstart", "loadstart"}));
}

TEST_F(WptResourceSelectionInvoke, SettingSrcInvokesTheLoadAlgorithm) {
  StartElement();
  BeginLoad();
  EXPECT_EQ(Pull(), (Names{"loadstart"}));
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
}

TEST_F(WptResourceSelectionInvoke, AnEmptySrcFiresLoadstartAndNeverLoads) {
  StartElement();
  SetSrc("");
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  EXPECT_TRUE(resolved_.empty());
}

TEST_F(WptResourceSelectionInvoke,
       DISABLED_LoadWithoutSrcIsNoSourceUntilTheTaskEnds) {
  StartElement();
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
  Do([this] {
    player_->Load();
    EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
  });
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptResourceSelectionInvoke,
       DISABLED_PlayWithoutSrcIsNoSourceUntilTheTaskEnds) {
  StartElement();
  Do([this] {
    player_->Play([](const PlayResult&) {});
    EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
  });
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptResourceSelectionInvoke,
       DISABLED_PauseWithoutSrcIsNoSourceUntilTheTaskEnds) {
  StartElement();
  Do([this] {
    player_->Pause();
    EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
  });
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptResourceSelectionInvoke, LoadWithoutSrcSettlesAtNetworkEmpty) {
  StartElement();
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
  Do([this] { player_->Load(); });
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptResourceSelectionInvoke, PlayWithoutSrcSettlesAtNetworkEmpty) {
  StartElement();
  PlayWithMarkers();
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptResourceSelectionInvoke, PauseWithoutSrcSettlesAtNetworkEmpty) {
  StartElement();
  Do([this] { player_->Pause(); });
  EXPECT_EQ(player_->network_state(), NetworkState::kEmpty);
}

TEST_F(WptMutedUserInterface, TogglingMutedRoundTripsAndSurvivesALoad) {
  StartElement();
  EXPECT_FALSE(player_->muted());
  Do([this] { player_->set_muted(!player_->muted()); });
  EXPECT_TRUE(player_->muted());
  Do([this] { player_->set_muted(!player_->muted()); });
  EXPECT_FALSE(player_->muted());
  Do([this] { player_->set_muted(true); });
  Do([this] { player_->set_muted(false); });
  SetSrc("");
  Pull();
  EXPECT_FALSE(player_->muted());
}

TEST_F(WptPreloadReflection, NoneIsKeptEvenWhenAutoplayIsSet) {
  StartElement();
  Do([this] {
    player_->set_autoplay(true);
    player_->set_preload(Preload::kNone);
  });
  EXPECT_EQ(player_->preload(), Preload::kNone);
}

TEST_F(WptPreservesPitch, DefaultsToTrueAndCanBeToggled) {
  StartElement();
  EXPECT_TRUE(player_->preserves_pitch());
  Do([this] { player_->set_preserves_pitch(false); });
  EXPECT_FALSE(player_->preserves_pitch());
  Do([this] { player_->set_preserves_pitch(true); });
  EXPECT_TRUE(player_->preserves_pitch());
}

TEST_F(WptMimeTypesCanPlayType, UnknownContainersAreNotSupported) {
  StartElement();
  for (const char* type :
       {"application/octet-stream",
        "application/octet-stream; codecs=\"vorbis\"",
        "application/octet-stream; codecs=\"avc1.42E01E, mp4a.40.2\"",
        "application/marks-fantasmagorical-format",
        "video/x-new-fictional-format",
        "video/x-new-fictional-format;codecs=\"kittens,bunnies\""}) {
    EXPECT_EQ(player_->CanPlayType(type), CanPlayTypeResult::kNone) << type;
  }
}

TEST_F(WptMimeTypesCanPlayType, AKnownContainerWithoutCodecsIsMaybe) {
  StartElement();
  for (const char* type : {"video/mp4", "audio/mp4", "video/webm"}) {
    EXPECT_EQ(player_->CanPlayType(type), CanPlayTypeResult::kMaybe) << type;
  }
}

TEST_F(WptMimeTypesCanPlayType,
       KnownCodecsOnAKnownContainerAreProbablySupported) {
  StartElement();
  for (const char* type :
       {"video/mp4; codecs=\"avc1.42E01E\"",
        "video/mp4; codecs=\"avc1.64001E\"", "video/mp4; codecs=\"mp4a.40.2\"",
        "audio/mp4; codecs=\"mp4a.40.2\""}) {
    EXPECT_EQ(player_->CanPlayType(type), CanPlayTypeResult::kProbably) << type;
  }
}

TEST_F(WptMimeTypesCanPlayType, ABogusCodecIsNotSupported) {
  StartElement();
  EXPECT_EQ(player_->CanPlayType("video/mp4; codecs=\"bogus\""),
            CanPlayTypeResult::kNone);
  EXPECT_EQ(player_->CanPlayType("video/webm; codecs=\"bogus\""),
            CanPlayTypeResult::kNone);
}

TEST_F(WptMimeTypesCanPlayType, CodecOrderDoesNotChangeTheAnswer) {
  StartElement();
  EXPECT_EQ(
      player_->CanPlayType("video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\""),
      player_->CanPlayType("video/mp4; codecs=\"mp4a.40.2, avc1.42E01E\""));
  EXPECT_EQ(
      player_->CanPlayType("video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\""),
      CanPlayTypeResult::kProbably);
}

TEST_F(WptMimeTypesCanPlayType, ADanglingParameterIsMaybe) {
  StartElement();
  for (const char* type :
       {"video/mp4;", "video/mp4;codecs", "video/mp4;codecs="}) {
    EXPECT_EQ(player_->CanPlayType(type), CanPlayTypeResult::kMaybe) << type;
  }
}

TEST_F(WptAutoplayOverridesPreload,
       AutoplayStartsPlaybackWhateverThePreloadHint) {
  for (const Preload preload : {Preload::kNone, Preload::kMetadata}) {
    for (const bool autoplay_first : {true, false}) {
      StartElement();
      BeginLoad();
      Do([&] {
        if (autoplay_first) {
          player_->set_autoplay(true);
          player_->set_preload(preload);
        } else {
          player_->set_preload(preload);
          player_->set_autoplay(true);
        }
      });
      Open();
      Preroll(4);
      const std::vector<ObservedEvent> events = PullEvents();
      const ObservedEvent* playing = FindEvent(events, "playing");
      ASSERT_NE(playing, nullptr)
          << static_cast<int>(preload) << autoplay_first;
      EXPECT_EQ(playing->state.ready_state, ReadyState::kHaveEnoughData);
      EXPECT_FALSE(playing->state.paused);
      EXPECT_FALSE(Contains(NamesOf(events), "error"));
    }
  }
}

TEST_F(WptReadyStatesAutoplay, AutoplayFiresCanplayCanplaythroughPlayPlaying) {
  StartElement();
  BeginLoad();
  Do([this] { player_->set_autoplay(true); });
  Open();
  Preroll(4);
  EXPECT_EQ(Only(Pull(), {"canplay", "canplaythrough", "pause", "play",
                          "playing", "error"}),
            (Names{"canplay", "canplaythrough", "play", "playing"}));
}

TEST_F(WptReadyStatesAutoplay, AutoplayAndLoadFiresTheSameSequence) {
  StartElement();
  BeginLoad();
  Do([this] {
    player_->set_autoplay(true);
    player_->Pause();
    player_->Load();
  });
  Open();
  Preroll(4);
  EXPECT_EQ(Only(Pull(), {"canplay", "canplaythrough", "pause", "play",
                          "playing", "error"}),
            (Names{"canplay", "canplaythrough", "play", "playing"}));
}

TEST_F(WptReadyStatesAutoplay, AutoplayAndPlayFiresPlayBeforeCanplay) {
  StartElement();
  BeginLoad();
  Do([this] { player_->set_autoplay(true); });
  PlayWithMarkers();
  EXPECT_FALSE(player_->paused());
  Open();
  Preroll(4);
  EXPECT_EQ(Only(Pull(), {"canplay", "canplaythrough", "pause", "play",
                          "playing", "error"}),
            (Names{"play", "canplay", "playing", "canplaythrough"}));
}

TEST_F(WptReadyStatesAutoplay, AutoplayAndPauseDoNotStartPlayback) {
  StartElement();
  BeginLoad();
  Do([this] {
    player_->set_autoplay(true);
    player_->Pause();
  });
  Open();
  Preroll(4);
  EXPECT_EQ(Only(Pull(), {"canplay", "canplaythrough", "pause", "play",
                          "playing", "error"}),
            (Names{"canplay", "canplaythrough"}));
  EXPECT_TRUE(player_->paused());
}

TEST_F(WptResourceSelectionInvoke,
       PauseWhenNetworkStateIsNotEmptyDoesNotReload) {
  StartElement();
  SetSrc("");
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
  Do([this] { player_->Pause(); });
  EXPECT_EQ(Pull(), Names{});
}

TEST_F(WptResourceSelectionInvoke,
       DISABLED_SettingSrcAfterPlayLeavesTheElementPaused) {
  StartElement();
  PlayWithMarkers();
  EXPECT_FALSE(player_->paused());
  BeginLoad();
  EXPECT_TRUE(player_->paused());
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
}

TEST_F(WptResourceSelectionInvoke,
       DISABLED_SettingAnEmptySrcAfterPlayLeavesTheElementPaused) {
  StartElement();
  PlayWithMarkers();
  EXPECT_FALSE(player_->paused());
  SetSrc("");
  EXPECT_TRUE(player_->paused());
  EXPECT_EQ(player_->network_state(), NetworkState::kNoSource);
}

TEST(WptPublicPlayer, DefaultsMatchTheHtmlInitialValues) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  EXPECT_FALSE(player->error().has_value());
  EXPECT_EQ(player->network_state(), NetworkState::kEmpty);
  EXPECT_EQ(player->ready_state(), ReadyState::kHaveNothing);
  EXPECT_TRUE(std::isnan(player->duration()));
  EXPECT_EQ(player->current_time(), 0.0);
  EXPECT_TRUE(player->paused());
  EXPECT_FALSE(player->ended());
  EXPECT_FALSE(player->seeking());
  EXPECT_EQ(player->playback_rate(), 1.0);
  EXPECT_EQ(player->volume(), 1.0);
  EXPECT_FALSE(player->muted());
}

TEST(WptPublicPlayer, AnEmptySrcFailsWithSrcNotSupported) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  player->set_src("");
  ASSERT_TRUE(player->error().has_value());
  EXPECT_EQ(static_cast<int>(player->error()->code()), 4);
  EXPECT_EQ(player->network_state(), NetworkState::kNoSource);
}

TEST(WptPublicPlayer, NonFiniteVolumeAndNegativeRateAreRejected) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  EXPECT_FALSE(player->set_volume(std::nan("")));
  EXPECT_FALSE(player->set_volume(std::numeric_limits<double>::infinity()));
  EXPECT_FALSE(player->set_playback_rate(-1.0));
  EXPECT_EQ(player->volume(), 1.0);
  EXPECT_EQ(player->playback_rate(), 1.0);
}

TEST_F(TrackListConformance, AudioAndVideoListsExposeTypedTracks) {
  StartElement();
  BeginLoad();
  Open();
  Pull();

  const std::vector<AudioTrack> audio = player_->audio_tracks();
  const std::vector<VideoTrack> video = player_->video_tracks();
  ASSERT_EQ(audio.size(), 2U);
  EXPECT_EQ(audio.front().kind, AudioTrackKind::kMain);
  EXPECT_EQ(audio.front().language, "en");
  EXPECT_TRUE(audio.front().enabled);
  ASSERT_EQ(video.size(), 2U);
  EXPECT_TRUE(video.front().selected);
  EXPECT_EQ(video.back().height, 720);
}

TEST_F(TrackListConformance, TrackSelectionDispatchesListChangeEvents) {
  StartElement();
  BeginLoad();
  Open();
  Pull();
  size_t audio_changes = 0;
  size_t video_changes = 0;
  Subscription audio_change;
  Subscription video_change;
  Do([&] {
    audio_change = player_->AddEventListener<AudioTracksChangeEvent>(
        [&](const AudioTracksChangeEvent&) { ++audio_changes; });
    video_change = player_->AddEventListener<VideoTracksChangeEvent>(
        [&](const VideoTracksChangeEvent&) { ++video_changes; });
    EXPECT_TRUE(player_->SelectAudioTrack("audio-es"));
    EXPECT_TRUE(player_->SelectVideoTrack("video-high"));
  });
  EXPECT_EQ(audio_changes, 0U);
  EXPECT_EQ(video_changes, 0U);
  Pull();
  EXPECT_EQ(audio_changes, 1U);
  EXPECT_EQ(video_changes, 1U);
  EXPECT_TRUE(player_->audio_tracks()[1].enabled);
  EXPECT_TRUE(player_->video_tracks()[1].selected);
  Do([&] { EXPECT_TRUE(player_->ClearVideoTrackSelection()); });
}

TEST_F(TrackListConformance, AddAndRemoveTrackEventsUsePlayerDispatch) {
  StartElement();
  size_t audio_added = 0;
  size_t audio_removed = 0;
  Subscription add;
  Subscription remove;
  Do([&] {
    add = player_->AddEventListener<AudioTrackAddEvent>(
        [&](const AudioTrackAddEvent&) { ++audio_added; });
    remove = player_->AddEventListener<AudioTrackRemoveEvent>(
        [&](const AudioTrackRemoveEvent&) { ++audio_removed; });
  });
  BeginLoad();
  Open();
  Pull();
  EXPECT_EQ(audio_added, 2U);

  SetSrc("https://example.test/next.mp4");
  source_->description.audio_tracks.pop_back();
  Open();
  Pull();
  EXPECT_EQ(audio_removed, 1U);
}

TEST(WptPublicPlayer, AddTextTrackFromUrlAddsTrackToList) {
  std::vector<std::function<void()>> posted;
  PlayerOptions options;
  options.post_task = [&](std::function<void()> task,
                          std::chrono::microseconds) {
    posted.push_back(std::move(task));
  };
  std::unique_ptr<Player> player = Player::Create(std::move(options));
  ASSERT_NE(player, nullptr);
  auto track =
      player->AddTextTrackFromUrl("file:///movie.srt", "English", "en");
  ASSERT_NE(track, nullptr);
  EXPECT_EQ(track->label(), "English");
  EXPECT_EQ(track->language(), "en");
  EXPECT_EQ(player->text_tracks().length(), 1U);
  EXPECT_EQ(player->text_tracks().item(0), track);
}

}
}
