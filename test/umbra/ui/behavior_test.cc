#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "harness.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/keyboard_map.h"
#include "umbra/ui/reduce.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {
namespace {

template <class T>
std::size_t Count(const Harness& harness) {
  std::size_t count = 0;
  for (const SequencedEffect& sequenced : harness.Effects()) {
    if (std::holds_alternative<T>(sequenced.effect)) {
      ++count;
    }
  }
  return count;
}

template <class T>
const T* LastOf(const Harness& harness) {
  const T* found = nullptr;
  for (const SequencedEffect& sequenced : harness.Effects()) {
    if (const auto* effect = std::get_if<T>(&sequenced.effect)) {
      found = effect;
    }
  }
  return found;
}

const ControlView* Find(const ViewModel& view_model, ControlId id) {
  return FindControl(view_model.controls.controls, id);
}

bool ChromeShown(Harness& harness) {
  return harness.Snapshot()->modes.chrome_visible;
}

std::shared_ptr<const MenuNode> SampleMenu() {
  MenuNode root;
  MenuNode quality{.id = "quality",
                   .label_key = "menu.quality",
                   .kind = MenuItemKind::kSubmenu,
                   .selected = false,
                   .children = {}};
  quality.children.push_back(MenuNode{.id = "quality.auto",
                                      .label_key = "menu.quality.auto",
                                      .kind = MenuItemKind::kChoice,
                                      .selected = true,
                                      .children = {}});
  quality.children.push_back(MenuNode{.id = "quality.1080",
                                      .label_key = "menu.quality.1080",
                                      .kind = MenuItemKind::kChoice,
                                      .selected = false,
                                      .children = {}});
  root.children.push_back(std::move(quality));
  root.children.push_back(MenuNode{.id = "captions",
                                   .label_key = "menu.captions",
                                   .kind = MenuItemKind::kToggle,
                                   .selected = false,
                                   .children = {}});
  return std::make_shared<const MenuNode>(std::move(root));
}

UiConfig TvConfig() {
  UiConfig config;
  config.layout = Layout::kTv;
  return config;
}

TEST(AutoHide, HidesAfterTheConfiguredInactivity) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(PointerActivity{});
  harness.AdvanceMillis(2999);
  EXPECT_TRUE(ChromeShown(harness));
  harness.AdvanceMillis(1);
  EXPECT_FALSE(ChromeShown(harness));
  EXPECT_FALSE(Find(*harness.Snapshot(), ControlId::kPlayPause)->visible);
}

TEST(AutoHide, ActivityMovesTheDeadlineWithoutAnotherTimer) {
  Harness harness;
  harness.Prepare(true);
  harness.AdvanceMillis(2000);
  harness.Send(PointerActivity{});
  harness.AdvanceMillis(2999);
  EXPECT_TRUE(ChromeShown(harness));
  harness.AdvanceMillis(1);
  EXPECT_FALSE(ChromeShown(harness));
  std::size_t auto_hide_timers = 0;
  for (const SequencedEffect& sequenced : harness.Effects()) {
    const auto* timer = std::get_if<StartTimer>(&sequenced.effect);
    if (timer != nullptr && timer->kind == TimerKind::kAutoHide) {
      ++auto_hide_timers;
    }
  }
  EXPECT_EQ(auto_hide_timers, 2U);
}

TEST(AutoHide, PausedPlaybackKeepsTheChromeShown) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(Pause{});
  harness.AdvanceMillis(60'000);
  EXPECT_TRUE(ChromeShown(harness));
}

TEST(AutoHide, PointerOverTheControlsPinsTheChrome) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(PointerEntered{});
  harness.AdvanceMillis(10'000);
  EXPECT_TRUE(ChromeShown(harness));
  harness.Send(PointerLeft{});
  harness.AdvanceMillis(2999);
  EXPECT_TRUE(ChromeShown(harness));
  harness.AdvanceMillis(1);
  EXPECT_FALSE(ChromeShown(harness));
}

TEST(AutoHide, ATapTogglesTheChrome) {
  Harness harness;
  harness.Prepare(false);
  harness.Send(ToggleControls{});
  EXPECT_FALSE(ChromeShown(harness));
  harness.Send(ToggleControls{});
  EXPECT_TRUE(ChromeShown(harness));
}

TEST(AutoHide, StaleTimersAreIgnored) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(TimerFired{.kind = TimerKind::kAutoHide, .generation = 9999});
  harness.Send(TimerFired{.kind = TimerKind::kSpinner, .generation = 1});
  harness.Send(TimerFired{.kind = TimerKind::kValueText, .generation = 77});
  EXPECT_TRUE(ChromeShown(harness));
}

TEST(Spinner, AppearsOnlyAfterTheDelay) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(Waiting{});
  harness.AdvanceMillis(499);
  EXPECT_FALSE(harness.Snapshot()->modes.spinner);
  harness.AdvanceMillis(1);
  const auto snapshot = harness.Snapshot();
  EXPECT_TRUE(snapshot->modes.spinner);
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kBuffering);
}

TEST(Spinner, ShortStallsNeverFlicker) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(Waiting{});
  harness.AdvanceMillis(100);
  harness.Send(PlaybackStarted{});
  harness.AdvanceMillis(1000);
  const auto snapshot = harness.Snapshot();
  EXPECT_FALSE(snapshot->modes.spinner);
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kPlaying);
}

TEST(Spinner, InitialLoadAlsoWaitsForTheDelay) {
  Harness harness;
  harness.Send(LoadStarted{});
  harness.AdvanceMillis(499);
  EXPECT_FALSE(harness.Snapshot()->modes.spinner);
  harness.AdvanceMillis(1);
  EXPECT_TRUE(harness.Snapshot()->modes.spinner);
}

TEST(ErrorPanel, ShowsTheCatalogueCodeAndAnnouncesAssertively) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(PlaybackFailed{.code = base::ErrorCode::kNetworkTimeout});
  const auto snapshot = harness.Snapshot();
  ASSERT_TRUE(snapshot->modes.error.has_value());
  EXPECT_EQ(snapshot->modes.error->code, base::ErrorCode::kNetworkTimeout);
  EXPECT_TRUE(snapshot->modes.error->retryable);
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kError);
  EXPECT_FALSE(Find(*snapshot, ControlId::kPlayPause)->enabled);
  ASSERT_FALSE(snapshot->announcements.queue.empty());
  EXPECT_EQ(snapshot->announcements.queue.back().key, "announce.error");
  EXPECT_EQ(snapshot->announcements.queue.back().politeness,
            Politeness::kAssertive);
  harness.AdvanceMillis(60'000);
  EXPECT_TRUE(ChromeShown(harness));
}

TEST(Scrubbing, PreviewFollowsTheThumbWithOneSeekOutstanding) {
  Harness harness;
  harness.Prepare(true);
  harness.ClearEffects();
  harness.Send(ScrubBegin{.fraction = 0.25});
  ASSERT_EQ(Count<PlayerSeek>(harness), 1U);
  EXPECT_EQ(LastOf<PlayerSeek>(harness)->position_us, 150'000'000);
  EXPECT_FALSE(LastOf<PlayerSeek>(harness)->precise);
  auto snapshot = harness.Snapshot();
  ASSERT_TRUE(snapshot->seek_bar.scrub.has_value());
  EXPECT_EQ(snapshot->seek_bar.scrub->position_us, 150'000'000);
  EXPECT_EQ(snapshot->seek_bar.playhead.rate, 0.0);
  harness.Send(ScrubMove{.fraction = 0.5});
  EXPECT_EQ(Count<PlayerSeek>(harness), 1U);
  EXPECT_EQ(harness.Snapshot()->seek_bar.scrub->position_us, 300'000'000);
  harness.Send(Seeked{.position_us = 150'000'000});
  ASSERT_EQ(Count<PlayerSeek>(harness), 2U);
  EXPECT_EQ(LastOf<PlayerSeek>(harness)->position_us, 300'000'000);
  harness.Send(ScrubEnd{});
  ASSERT_EQ(Count<PlayerSeek>(harness), 3U);
  EXPECT_TRUE(LastOf<PlayerSeek>(harness)->precise);
  snapshot = harness.Snapshot();
  EXPECT_FALSE(snapshot->seek_bar.scrub.has_value());
  EXPECT_EQ(snapshot->seek_bar.playhead.position_us, 300'000'000);
  EXPECT_EQ(snapshot->seek_bar.playhead.rate, 0.0);
  harness.Send(Seeked{.position_us = 300'000'000});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 1.0);
}

TEST(Scrubbing, PinsTheChromeUntilItEnds) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(ScrubBegin{.fraction = 0.1});
  harness.AdvanceMillis(10'000);
  EXPECT_TRUE(ChromeShown(harness));
  harness.Send(ScrubCancel{});
  harness.AdvanceMillis(3000);
  EXPECT_FALSE(ChromeShown(harness));
}

TEST(Playback, TogglePlayFlipsAtOnceAndTheEngineCorrectsIt) {
  Harness harness;
  harness.Prepare(false);
  harness.ClearEffects();
  harness.Send(TogglePlay{});
  EXPECT_EQ(Count<PlayerPlay>(harness), 1U);
  auto snapshot = harness.Snapshot();
  EXPECT_EQ(Find(*snapshot, ControlId::kPlayPause)->label_key, "control.pause");
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kPlaying);
  harness.Send(PlayRejected{});
  snapshot = harness.Snapshot();
  EXPECT_EQ(Find(*snapshot, ControlId::kPlayPause)->label_key, "control.play");
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kPaused);
}

TEST(Playback, EndedPlaybackRestartsFromTheBeginning) {
  Harness harness;
  harness.Prepare(true);
  harness.AdvanceMillis(5000);
  harness.Send(PlaybackEnded{});
  EXPECT_EQ(harness.Snapshot()->modes.phase, PlaybackPhase::kEnded);
  harness.Send(TogglePlay{});
  const auto snapshot = harness.Snapshot();
  EXPECT_EQ(snapshot->modes.phase, PlaybackPhase::kPlaying);
  EXPECT_EQ(snapshot->seek_bar.playhead.position_us, 0);
}

TEST(Playback, ControlsAreUnavailableWithoutASource) {
  Harness harness;
  const auto snapshot = harness.Snapshot();
  EXPECT_FALSE(Find(*snapshot, ControlId::kPlayPause)->enabled);
  harness.Send(TogglePlay{});
  EXPECT_TRUE(harness.Effects().empty());
}

TEST(Playhead, SteadyPlaybackPublishesNothingPerTimeUpdate) {
  Harness harness;
  harness.Prepare(true);
  const auto before = harness.Snapshot();
  harness.AdvanceMillis(10'000);
  harness.Send(TimeUpdate{.position_us = 10'050'000, .anchor_us = 0});
  const auto steady = harness.Snapshot();
  EXPECT_EQ(steady->seek_bar.version, before->seek_bar.version);
  harness.Send(TimeUpdate{.position_us = 11'000'000, .anchor_us = 0});
  const auto corrected = harness.Snapshot();
  EXPECT_EQ(corrected->seek_bar.version, before->seek_bar.version + 1);
  EXPECT_EQ(corrected->seek_bar.playhead.position_us, 11'000'000);
}

TEST(Playhead, RateIsThePlaybackRateTimesTheResampleRatio) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(RateChanged{.rate = 2.0, .resample_ratio = 0.5});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 1.0);
  harness.Send(RateChanged{.rate = 1.5, .resample_ratio = 1.0});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 1.5);
  harness.Send(Waiting{});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 0.0);
  harness.Send(PlaybackStarted{});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 1.5);
  harness.Send(Pause{});
  EXPECT_EQ(harness.Snapshot()->seek_bar.playhead.rate, 0.0);
}

TEST(Playhead, ARateChangeKeepsTheElapsedPosition) {
  Harness harness;
  harness.Prepare(true);
  harness.AdvanceMillis(4000);
  harness.Send(Pause{});
  const Playhead playhead = harness.Snapshot()->seek_bar.playhead;
  EXPECT_EQ(playhead.position_us, 4'000'000);
  EXPECT_EQ(playhead.anchor_us, 4'000'000);
}

TEST(Playhead, ValueTextFollowsPlaybackAtACoarseInterval) {
  Harness harness;
  harness.Prepare(true);
  const auto text = [&harness] {
    return std::get<std::int64_t>(harness.Snapshot()
                                      ->controls.controls.front()
                                      .value_text->args.front()
                                      .value);
  };
  EXPECT_EQ(text(), 0);
  harness.AdvanceMillis(4999);
  EXPECT_EQ(text(), 0);
  harness.AdvanceMillis(1);
  EXPECT_EQ(text(), 5);
  harness.AdvanceMillis(5000);
  EXPECT_EQ(text(), 10);
}

TEST(Playhead, LiveStreamsDescribeTheDistanceBehindTheEdge) {
  Harness harness;
  harness.Send(LoadStarted{});
  harness.Send(DurationChanged{.duration_us = 0, .live = true});
  harness.Send(SeekableChanged{.start_us = 100'000'000, .end_us = 160'000'000});
  harness.Send(CanPlay{});
  const auto snapshot = harness.Snapshot();
  const ControlView* seek = Find(*snapshot, ControlId::kSeekBar);
  ASSERT_TRUE(seek->value_text.has_value());
  EXPECT_EQ(seek->value_text->key, "value.live");
  EXPECT_TRUE(snapshot->seek_bar.live);
  EXPECT_TRUE(seek->enabled);
}

TEST(Publication, OneFrameRequestPerFrameAndNothingWhenNothingChanges) {
  Harness harness;
  harness.Prepare(true);
  static_cast<void>(harness.Snapshot());
  int requests = 0;
  harness.Machine().SetFrameRequestHook([&requests] { ++requests; });
  harness.Send(PointerActivity{});
  EXPECT_EQ(requests, 0);
  harness.Send(ToggleMute{});
  harness.Send(ToggleMute{});
  harness.Send(ToggleMute{});
  EXPECT_EQ(requests, 1);
  static_cast<void>(harness.Snapshot());
  harness.Send(ToggleMute{});
  EXPECT_EQ(requests, 2);
}

TEST(Publication, OnlyChangedSectionsGetANewVersion) {
  Harness harness;
  harness.Prepare(true);
  const auto before = harness.Snapshot();
  harness.Send(ToggleMute{});
  const auto after = harness.Snapshot();
  EXPECT_EQ(after->controls.version, before->controls.version + 1);
  EXPECT_EQ(after->seek_bar.version, before->seek_bar.version);
  EXPECT_EQ(after->focus.version, before->focus.version);
  EXPECT_EQ(after->menu.version, before->menu.version);
  EXPECT_EQ(after->modes.version, before->modes.version);
}

TEST(Announcements, AreDeliveredOnceThroughASnapshot) {
  Harness harness;
  harness.Prepare(false);
  harness.Send(TogglePlay{});
  auto snapshot = harness.Snapshot();
  ASSERT_EQ(snapshot->announcements.queue.size(), 1U);
  EXPECT_EQ(snapshot->announcements.queue[0].key, "announce.play");
  EXPECT_EQ(harness.Snapshot()->announcements.queue.size(), 1U);
  harness.Send(TogglePlay{});
  snapshot = harness.Snapshot();
  ASSERT_EQ(snapshot->announcements.queue.size(), 1U);
  EXPECT_EQ(snapshot->announcements.queue[0].key, "announce.pause");
  EXPECT_EQ(snapshot->announcements.queue[0].sequence, 2U);
}

TEST(Announcements, CoalescedFramesDoNotDropThem) {
  Harness harness;
  harness.Prepare(false);
  harness.Send(TogglePlay{});
  harness.Send(TogglePlay{});
  harness.Send(TogglePlay{});
  EXPECT_EQ(harness.Snapshot()->announcements.queue.size(), 3U);
}

TEST(Announcements, ASeekThatLandsIsAnnounced) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(SeekSteps{.steps = 1});
  harness.Send(Seeked{.position_us = 5'000'000});
  const auto snapshot = harness.Snapshot();
  EXPECT_EQ(snapshot->announcements.queue.back().key, "announce.seeked");
}

TEST(Menu, OpeningPinsTheChromeAndBackUnwindsInOrder) {
  Harness harness;
  harness.Send(SetMenu{.root = SampleMenu()});
  harness.Prepare(true);
  EXPECT_TRUE(Find(*harness.Snapshot(), ControlId::kSettings)->visible);
  harness.Send(Activate{.id = ControlId::kSettings});
  EXPECT_TRUE(harness.Snapshot()->menu.open);
  EXPECT_EQ(Find(*harness.Snapshot(), ControlId::kSettings)->expanded,
            std::optional<bool>(true));
  harness.AdvanceMillis(10'000);
  EXPECT_TRUE(ChromeShown(harness));
  harness.Send(Navigate{.direction = Direction::kDown});
  EXPECT_EQ(harness.Snapshot()->menu.highlighted, 1U);
  harness.Send(Navigate{.direction = Direction::kUp});
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_EQ(harness.Snapshot()->menu.path, std::vector<std::uint32_t>{0});
  harness.Send(Back{});
  EXPECT_TRUE(harness.Snapshot()->menu.path.empty());
  EXPECT_TRUE(harness.Snapshot()->menu.open);
  harness.Send(Back{});
  EXPECT_FALSE(harness.Snapshot()->menu.open);
  EXPECT_TRUE(ChromeShown(harness));
  harness.ClearEffects();
  harness.Send(Back{});
  EXPECT_FALSE(ChromeShown(harness));
  harness.Send(Back{});
  EXPECT_EQ(Count<BackToApp>(harness), 1U);
}

TEST(Menu, ChoicesNotifyTheAppAndCloseTheMenu) {
  Harness harness;
  harness.Send(SetMenu{.root = SampleMenu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kDown});
  harness.Send(ActivateFocused{});
  ASSERT_EQ(Count<MenuChoice>(harness), 1U);
  EXPECT_EQ(LastOf<MenuChoice>(harness)->item_id, "quality.1080");
  EXPECT_FALSE(harness.Snapshot()->menu.open);
}

TEST(Menu, TogglesStayOpen) {
  Harness harness;
  harness.Send(SetMenu{.root = SampleMenu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(Navigate{.direction = Direction::kDown});
  harness.Send(ActivateFocused{});
  EXPECT_EQ(LastOf<MenuChoice>(harness)->item_id, "captions");
  EXPECT_TRUE(harness.Snapshot()->menu.open);
}

TEST(Menu, RightToLeftMirrorsSubmenuNavigation) {
  UiConfig config;
  config.rtl = true;
  Harness harness(config);
  harness.Send(SetMenu{.root = SampleMenu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(Navigate{.direction = Direction::kLeft});
  EXPECT_EQ(harness.Snapshot()->menu.path.size(), 1U);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_TRUE(harness.Snapshot()->menu.path.empty());
  EXPECT_TRUE(harness.Snapshot()->menu.open);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_FALSE(harness.Snapshot()->menu.open);
}

TEST(Menu, ThereIsNoSettingsButtonWithoutAMenu) {
  Harness harness;
  harness.Prepare(true);
  EXPECT_FALSE(Find(*harness.Snapshot(), ControlId::kSettings)->visible);
  harness.Send(OpenMenu{});
  EXPECT_FALSE(harness.Snapshot()->menu.open);
}

TEST(AdMode, ClosesTheMenuAndHidesWhatItReduces) {
  Harness harness;
  harness.Send(SetMenu{.root = SampleMenu()});
  harness.Prepare(true);
  harness.Send(Activate{.id = ControlId::kSettings});
  ASSERT_TRUE(harness.Snapshot()->menu.open);
  harness.Send(AdBreakStarted{.owner = AdInputOwner::kUi, .skippable = true});
  auto snapshot = harness.Snapshot();
  EXPECT_FALSE(snapshot->menu.open);
  EXPECT_TRUE(snapshot->modes.ad_active);
  EXPECT_FALSE(Find(*snapshot, ControlId::kSeekBar)->visible);
  EXPECT_FALSE(Find(*snapshot, ControlId::kSettings)->visible);
  EXPECT_TRUE(Find(*snapshot, ControlId::kSkipAd)->visible);
  EXPECT_FALSE(Find(*snapshot, ControlId::kSkipAd)->enabled);
  EXPECT_EQ(snapshot->announcements.queue.back().key, "announce.ad_started");
  harness.ClearEffects();
  harness.Send(Activate{.id = ControlId::kSeekBar});
  harness.Send(Activate{.id = ControlId::kSettings});
  harness.Send(SeekSteps{.steps = 1});
  harness.Send(SeekToFraction{.fraction = 0.5});
  harness.Send(ScrubBegin{.fraction = 0.5});
  harness.Send(OpenMenu{});
  harness.Send(Activate{.id = ControlId::kSkipAd});
  EXPECT_TRUE(harness.Effects().empty());
  harness.Send(AdSkipAvailable{});
  EXPECT_TRUE(Find(*harness.Snapshot(), ControlId::kSkipAd)->enabled);
  harness.Send(Activate{.id = ControlId::kSkipAd});
  EXPECT_EQ(Count<SkipAdRequested>(harness), 1U);
  harness.Send(AdBreakEnded{});
  snapshot = harness.Snapshot();
  EXPECT_FALSE(snapshot->modes.ad_active);
  EXPECT_TRUE(Find(*snapshot, ControlId::kSeekBar)->visible);
}

TEST(AdMode, FocusMovesToTheSkipControlWhenItBecomesAvailable) {
  Harness harness(TvConfig());
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  ASSERT_EQ(harness.Snapshot()->focus.target, ControlId::kPlayPause);
  harness.Send(AdBreakStarted{.owner = AdInputOwner::kUi, .skippable = true});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kPlayPause);
  harness.Send(AdSkipAvailable{});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kSkipAd);
  harness.Send(AdBreakEnded{});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
}

TEST(AdMode, ProviderOwnedBreaksIgnoreControlInput) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(
      AdBreakStarted{.owner = AdInputOwner::kProvider, .skippable = false});
  harness.ClearEffects();
  harness.Send(TogglePlay{});
  harness.Send(ToggleMute{});
  harness.Send(Activate{.id = ControlId::kPlayPause});
  EXPECT_TRUE(harness.Effects().empty());
  const auto snapshot = harness.Snapshot();
  EXPECT_FALSE(Find(*snapshot, ControlId::kPlayPause)->enabled);
  harness.Send(Back{});
  EXPECT_FALSE(ChromeShown(harness));
}

TEST(Focus, DirectionsWakeTheChromeAndMoveFocusOnTv) {
  Harness harness(TvConfig());
  harness.Prepare(true);
  harness.AdvanceMillis(3000);
  ASSERT_FALSE(ChromeShown(harness));
  harness.Send(Navigate{.direction = Direction::kRight});
  auto snapshot = harness.Snapshot();
  EXPECT_TRUE(snapshot->modes.chrome_visible);
  EXPECT_EQ(snapshot->focus.target, ControlId::kPlayPause);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kMute);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kVolume);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
  harness.Send(Navigate{.direction = Direction::kRight});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
  harness.Send(Navigate{.direction = Direction::kUp});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kSeekBar);
  harness.Send(Navigate{.direction = Direction::kDown});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kPlayPause);
}

TEST(Focus, SlidersConsumeTheirOwnAxis) {
  Harness harness(TvConfig());
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kUp});
  harness.Send(Navigate{.direction = Direction::kUp});
  ASSERT_EQ(harness.Snapshot()->focus.target, ControlId::kSeekBar);
  harness.ClearEffects();
  harness.Send(Navigate{.direction = Direction::kRight});
  ASSERT_EQ(Count<PlayerSeek>(harness), 1U);
  EXPECT_EQ(LastOf<PlayerSeek>(harness)->position_us, 5'000'000);
  harness.Send(Navigate{.direction = Direction::kDown});
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kRight});
  ASSERT_EQ(harness.Snapshot()->focus.target, ControlId::kVolume);
  harness.ClearEffects();
  harness.Send(Navigate{.direction = Direction::kDown});
  ASSERT_EQ(Count<PlayerSetVolume>(harness), 1U);
  EXPECT_EQ(LastOf<PlayerSetVolume>(harness)->volume, 0.95);
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kVolume);
}

TEST(Focus, ReturnsToTheRestoreTargetWhenTheChromeReappears) {
  Harness harness(TvConfig());
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(Navigate{.direction = Direction::kRight});
  ASSERT_EQ(harness.Snapshot()->focus.target, ControlId::kMute);
  harness.AdvanceMillis(3000);
  auto snapshot = harness.Snapshot();
  EXPECT_FALSE(snapshot->focus.target.has_value());
  EXPECT_EQ(snapshot->focus.restore, ControlId::kMute);
  harness.Send(Navigate{.direction = Direction::kLeft});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kMute);
}

TEST(Focus, APlatformFocusChangeIsAcceptedOnlyForFocusableControls) {
  Harness harness(TvConfig());
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(FocusControl{.id = ControlId::kFullscreen});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
  harness.Send(FocusControl{.id = ControlId::kSkipAd});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
}

TEST(Focus, ABecomingHiddenControlPassesFocusToItsNeighbour) {
  Harness harness(TvConfig());
  harness.Send(RegisterControl{.name = "cast",
                               .label_key = "app.cast",
                               .icon_token = "app.icon_cast",
                               .slot = Slot::kTrailing});
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  harness.Send(FocusControl{.id = ControlId::kFirstCustom});
  ASSERT_EQ(harness.Snapshot()->focus.target, ControlId::kFirstCustom);
  harness.Send(UpdateControl{.name = "cast",
                             .visible = false,
                             .enabled = std::nullopt,
                             .pressed = std::nullopt});
  EXPECT_EQ(harness.Snapshot()->focus.target, ControlId::kFullscreen);
}

TEST(CustomControls, JoinTheModelAndEmitCustomActions) {
  Harness harness;
  harness.Send(RegisterControl{.name = "cast",
                               .label_key = "app.cast",
                               .icon_token = "app.icon_cast",
                               .slot = Slot::kLeading});
  harness.Send(RegisterControl{.name = "cast",
                               .label_key = "app.other",
                               .icon_token = "x",
                               .slot = Slot::kLeading});
  harness.Prepare(true);
  const auto snapshot = harness.Snapshot();
  const ControlView* cast = Find(*snapshot, ControlId::kFirstCustom);
  ASSERT_NE(cast, nullptr);
  EXPECT_EQ(cast->kind, ControlKind::kCustom);
  EXPECT_EQ(cast->label_key, "app.cast");
  EXPECT_TRUE(cast->focusable);
  EXPECT_EQ(snapshot->controls.controls.size(), 8U);
  harness.Send(Activate{.id = ControlId::kFirstCustom});
  ASSERT_EQ(Count<CustomAction>(harness), 1U);
  EXPECT_EQ(LastOf<CustomAction>(harness)->name, "cast");
  harness.Send(UpdateControl{.name = "cast",
                             .visible = std::nullopt,
                             .enabled = false,
                             .pressed = true});
  const ControlView* updated =
      Find(*harness.Snapshot(), ControlId::kFirstCustom);
  EXPECT_FALSE(updated->enabled);
  EXPECT_EQ(updated->pressed, std::optional<bool>(true));
  harness.Send(Activate{.id = ControlId::kFirstCustom});
  EXPECT_EQ(Count<CustomAction>(harness), 1U);
}

TEST(Settings, HiddenControlsLeaveTheModelAndOrderIsApplied) {
  UiConfig config;
  config.hidden_controls = {"mute"};
  config.control_order = {"volume", "play_pause"};
  Harness harness(config);
  harness.Prepare(true);
  std::vector<std::string> leading;
  for (const ControlView& control : harness.Snapshot()->controls.controls) {
    if (control.slot == Slot::kLeading) {
      leading.push_back(control.name);
    }
  }
  EXPECT_EQ(leading, (std::vector<std::string>{"volume", "play_pause"}));
  harness.ClearEffects();
  harness.Send(Activate{.id = ControlId::kMute});
  EXPECT_TRUE(harness.Effects().empty());
}

TEST(Keyboard, TheMapIsDataSharedByRenderers) {
  const auto toggle = KeyToIntent("Space");
  ASSERT_TRUE(toggle.has_value());
  EXPECT_TRUE(std::holds_alternative<TogglePlay>(*toggle));
  EXPECT_TRUE(std::holds_alternative<TogglePlay>(*KeyToIntent("k")));
  EXPECT_TRUE(std::holds_alternative<ToggleMute>(*KeyToIntent("m")));
  EXPECT_TRUE(std::holds_alternative<ToggleFullscreen>(*KeyToIntent("f")));
  EXPECT_FALSE(KeyToIntent("x").has_value());
  std::set<std::string_view> keys;
  for (const KeyBinding& binding : kKeyboardMap) {
    keys.insert(binding.key);
  }
  EXPECT_EQ(keys.size(), kKeyboardMap.size());
}

TEST(Keyboard, DesktopArrowsSeekAndChangeTheVolume) {
  Harness harness;
  harness.Prepare(true);
  harness.ClearEffects();
  harness.Send(*KeyToIntent("ArrowRight"));
  ASSERT_EQ(Count<PlayerSeek>(harness), 1U);
  EXPECT_EQ(LastOf<PlayerSeek>(harness)->position_us, 5'000'000);
  harness.Send(*KeyToIntent("j"));
  EXPECT_EQ(LastOf<PlayerSeek>(harness)->position_us, 0);
  harness.Send(*KeyToIntent("ArrowDown"));
  ASSERT_EQ(Count<PlayerSetVolume>(harness), 1U);
  EXPECT_EQ(LastOf<PlayerSetVolume>(harness)->volume, 0.95);
  harness.Send(*KeyToIntent("ArrowUp"));
  EXPECT_EQ(LastOf<PlayerSetVolume>(harness)->volume, 1.0);
  harness.Send(*KeyToIntent("Space"));
  EXPECT_EQ(Count<PlayerPause>(harness), 1U);
}

TEST(Keyboard, UnmutingByVolumeUnmutes) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(ToggleMute{});
  EXPECT_TRUE(harness.State().media.muted);
  harness.Send(SetVolume{.volume = 0.5});
  EXPECT_FALSE(harness.State().media.muted);
  EXPECT_EQ(LastOf<PlayerSetMuted>(harness)->muted, false);
}

TEST(Presentation, FullscreenIsRequestedAndConfirmedByTheEngine) {
  Harness harness;
  harness.Prepare(true);
  harness.Send(ToggleFullscreen{});
  ASSERT_EQ(Count<RequestPresentation>(harness), 1U);
  EXPECT_EQ(LastOf<RequestPresentation>(harness)->mode,
            PresentationMode::kFullscreen);
  EXPECT_EQ(harness.Snapshot()->modes.presentation, PresentationMode::kInline);
  harness.Send(PresentationChanged{.mode = PresentationMode::kFullscreen});
  const auto snapshot = harness.Snapshot();
  EXPECT_EQ(Find(*snapshot, ControlId::kFullscreen)->pressed,
            std::optional<bool>(true));
  harness.Send(ToggleFullscreen{});
  EXPECT_EQ(LastOf<RequestPresentation>(harness)->mode,
            PresentationMode::kInline);
}

TEST(Invariants, HoldForTheInitialState) {
  EXPECT_TRUE(CheckInvariants(InitialState(), UiConfig{}).empty());
}

}
}
