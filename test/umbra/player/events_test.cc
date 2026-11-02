#include "umbra/player/events.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "player_fixture.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"

namespace umbra::player {
namespace {

using Names = std::vector<std::string>;
using penumbra::PlayerState;
using test::PlayerFixture;

class Recorder {
 public:
  explicit Recorder(Player& player) {
    Listen<LoadStartEvent>(player);
    Listen<ProgressEvent>(player);
    Listen<SuspendEvent>(player);
    Listen<AbortEvent>(player);
    Listen<ErrorEvent>(player);
    Listen<EmptiedEvent>(player);
    Listen<StalledEvent>(player);
    Listen<LoadedMetadataEvent>(player);
    Listen<LoadedDataEvent>(player);
    Listen<CanPlayEvent>(player);
    Listen<CanPlayThroughEvent>(player);
    Listen<PlayingEvent>(player);
    Listen<WaitingEvent>(player);
    Listen<SeekingEvent>(player);
    Listen<SeekedEvent>(player);
    Listen<EndedEvent>(player);
    Listen<DurationChangeEvent>(player);
    Listen<TimeUpdateEvent>(player);
    Listen<PlayEvent>(player);
    Listen<PauseEvent>(player);
    Listen<RateChangeEvent>(player);
    Listen<ResizeEvent>(player);
    Listen<VolumeChangeEvent>(player);
    Listen<WarningEvent>(player);
  }

  Names Take() {
    Names taken = std::move(names_);
    names_.clear();
    return taken;
  }

  void Add(const std::string& name) { names_.push_back(name); }

  [[nodiscard]] const ElementState& StateOf(const std::string& name) const {
    return states_.at(name);
  }

 private:
  template <typename Event>
  void Listen(Player& player) {
    subscriptions_.push_back(
        player.AddEventListener<Event>([this](const Event& event) {
          names_.emplace_back(Event::kName);
          states_[std::string(Event::kName)] = event.state;
        }));
  }

  Names names_;
  std::map<std::string, ElementState> states_;
  std::vector<Subscription> subscriptions_;
};

class EventsTest : public PlayerFixture {
 protected:
  void Start(const std::string& url = "https://example.test/a.mp4",
             int64_t duration_ms = 3000) {
    Create();
    recorder_ = std::make_unique<Recorder>(*player_);
    SetSrc(url, duration_ms);
  }

  Names Pull() {
    Dispatch();
    return recorder_->Take();
  }

  Names PullWithoutProgress() {
    Names names = Pull();
    std::erase(names, "progress");
    return names;
  }

  void Play() {
    Do([this] {
      player_->Play([this](const PlayResult& result) {
        recorder_->Add(result.code == PlayResultCode::kOk ? "result:ok"
                                                          : "result:other");
      });
    });
  }

  void ToPlaying() {
    Start();
    Open();
    Preroll();
    Pull();
    Play();
    PortState(PlayerState::kPresenting);
    Pull();
  }

  std::unique_ptr<Recorder> recorder_;
};

TEST_F(EventsTest, LoadSequenceMatchesTheHtmlOrder) {
  Start();
  EXPECT_EQ(Pull(), (Names{"loadstart"}));
  Open();
  EXPECT_EQ(Pull(), (Names{"durationchange", "resize", "loadedmetadata"}));
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadeddata", "canplay", "canplaythrough"}));
}

TEST_F(EventsTest, AudioOnlyMediaFiresNoResize) {
  Start();
  source_->description.streams.erase(source_->description.streams.begin());
  Open();
  EXPECT_EQ(Pull(), (Names{"loadstart", "durationchange", "loadedmetadata"}));
}

TEST_F(EventsTest, PlaySequenceOrdersTheResultAfterPlaying) {
  Start();
  Open();
  Preroll();
  Pull();
  Play();
  EXPECT_EQ(Pull(), (Names{"play", "playing", "result:ok"}));
}

TEST_F(EventsTest, PlayBeforeDataWaitsThenPlays) {
  Start();
  Open();
  Pull();
  Play();
  EXPECT_EQ(Pull(), (Names{"play", "waiting"}));
  Preroll();
  EXPECT_EQ(Pull(), (Names{"loadeddata", "canplay", "playing", "result:ok",
                           "canplaythrough"}));
}

TEST_F(EventsTest, PauseSequenceFiresTimeUpdateThenPause) {
  ToPlaying();
  Do([this] { player_->Pause(); });
  EXPECT_EQ(Pull(), (Names{"timeupdate", "pause"}));
  EXPECT_TRUE(recorder_->StateOf("pause").paused);
}

TEST_F(EventsTest, PausingRejectsThePendingPlayAfterThePauseEvent) {
  Start();
  Open();
  Pull();
  Play();
  Do([this] { player_->Pause(); });
  EXPECT_EQ(Pull(),
            (Names{"play", "waiting", "timeupdate", "pause", "result:other"}));
}

TEST_F(EventsTest, SeekSequenceMatchesTheHtmlOrder) {
  ToPlaying();
  Do([this] { EXPECT_TRUE(player_->set_current_time(1.0)); });
  EXPECT_EQ(Pull(), (Names{"seeking", "timeupdate", "waiting"}));
  EXPECT_TRUE(recorder_->StateOf("seeking").seeking);
  EXPECT_EQ(recorder_->StateOf("seeking").current_time, 1.0);
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  PortState(PlayerState::kReady);
  const Names names = Pull();
  ASSERT_GE(names.size(), 2U);
  EXPECT_EQ(names[0], "timeupdate");
  EXPECT_EQ(names[1], "seeked");
  EXPECT_FALSE(recorder_->StateOf("seeked").seeking);
}

TEST_F(EventsTest, EndSequenceMatchesTheHtmlOrder) {
  Start("https://example.test/a.mp4", 600);
  Open();
  Preroll();
  Pull();
  Play();
  PortState(PlayerState::kPresenting);
  Pull();
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  EXPECT_EQ(Pull(),
            (Names{"progress", "suspend", "timeupdate", "pause", "ended"}));
  const ElementState ended = recorder_->StateOf("ended");
  EXPECT_TRUE(ended.ended);
  EXPECT_TRUE(ended.paused);
  EXPECT_EQ(ended.network_state, NetworkState::kIdle);
}

TEST_F(EventsTest, ErrorSequenceCarriesTheMediaError) {
  Start();
  std::optional<MediaError> seen;
  const Subscription subscription = player_->AddEventListener<ErrorEvent>(
      [&](const ErrorEvent& event) { seen = event.error; });
  Do([this] {
    source_->FailOpen(test::DetailFor(base::ErrorCode::kNetworkAccessDenied));
  });
  EXPECT_EQ(Pull(), (Names{"loadstart", "error"}));
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->code(), MediaErrorCode::kSrcNotSupported);
  EXPECT_EQ(seen->detail().code, base::ErrorCode::kNetworkAccessDenied);
  EXPECT_EQ(seen->message().rfind("ALUMBRA-1002", 0), 0U);
  EXPECT_EQ(recorder_->StateOf("error").network_state, NetworkState::kNoSource);
}

TEST_F(EventsTest, ErrorAfterMetadataKeepsTheCatalogueMapping) {
  Start();
  std::optional<MediaError> seen;
  const Subscription subscription = player_->AddEventListener<ErrorEvent>(
      [&](const ErrorEvent& event) { seen = event.error; });
  Open();
  Do([this] {
    source_->sink->OnSourceError(
        test::DetailFor(base::ErrorCode::kMediaCorrupt));
  });
  Pull();
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->code(), MediaErrorCode::kDecode);
  EXPECT_EQ(recorder_->StateOf("error").network_state, NetworkState::kIdle);
}

TEST_F(EventsTest, EveryCatalogueCodeMapsThroughThePlayerAfterMetadata) {
  for (const auto& entry : base::kErrorCatalogue) {
    Start();
    Open();
    base::ErrorDetail detail(entry.code, entry.category,
                             base::ErrorSeverity::kFatal, entry.retryable, {},
                             nullptr);
    Do([&] { source_->sink->OnSourceError(detail); });
    const std::optional<MediaError> error = player_->error();
    ASSERT_TRUE(error.has_value()) << entry.name;
    EXPECT_EQ(static_cast<int>(error->code()), entry.media_error) << entry.name;
  }
}

TEST_F(EventsTest, ReloadFiresAbortEmptiedAndLoadStart) {
  Start();
  Open();
  Pull();
  Do([this] { player_->Load(); });
  EXPECT_EQ(Pull(), (Names{"abort", "emptied", "loadstart"}));
}

TEST_F(EventsTest, RateAndVolumeChangesFireTheirEvents) {
  Start();
  Pull();
  Do([this] {
    EXPECT_TRUE(player_->set_playback_rate(2.0));
    EXPECT_TRUE(player_->set_volume(0.5));
    player_->set_muted(true);
  });
  EXPECT_EQ(Pull(), (Names{"ratechange", "volumechange", "volumechange"}));
}

TEST_F(EventsTest, DegradedErrorsRaiseAWarningAndPlaybackContinues) {
  ToPlaying();
  std::optional<base::ErrorDetail> seen;
  const Subscription subscription = player_->AddEventListener<WarningEvent>(
      [&](const WarningEvent& event) { seen = event.detail; });
  Do([this] {
    source_->sink->OnSourceError(
        test::DetailFor(base::ErrorCode::kDecoderFallback));
  });
  EXPECT_EQ(Pull(), (Names{"warning"}));
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->code, base::ErrorCode::kDecoderFallback);
  EXPECT_EQ(seen->severity, base::ErrorSeverity::kDegraded);
  EXPECT_FALSE(player_->error().has_value());
  EXPECT_FALSE(player_->paused());
}

TEST_F(EventsTest, RecoverableErrorsAreNotAppEvents) {
  ToPlaying();
  Do([this] {
    source_->sink->OnSourceError(
        test::DetailFor(base::ErrorCode::kNetworkTimeout));
  });
  EXPECT_EQ(Pull(), Names{});
}

TEST_F(EventsTest, TimeUpdateFiresAtTheConfiguredIntervalUnderTheFakeClock) {
  options_.timeupdate_interval_ms = 100;
  ToPlaying();
  for (int tick = 0; tick < 5; ++tick) {
    env_.FastForwardBy(absl::Milliseconds(100));
    EXPECT_EQ(PullWithoutProgress(), (Names{"timeupdate"})) << tick;
  }
}

TEST_F(EventsTest, TimeUpdateDefaultsToTwoHundredAndFiftyMilliseconds) {
  ToPlaying();
  env_.FastForwardBy(absl::Milliseconds(249));
  EXPECT_EQ(Pull(), Names{});
  env_.FastForwardBy(absl::Milliseconds(1));
  EXPECT_EQ(Pull(), (Names{"timeupdate"}));
}

TEST_F(EventsTest, PeriodicTimeUpdatesAreCoalescedWhileOneIsPending) {
  options_.timeupdate_interval_ms = 100;
  ToPlaying();
  env_.FastForwardBy(absl::Milliseconds(1000));
  EXPECT_EQ(PullWithoutProgress(), (Names{"timeupdate"}));
}

TEST_F(EventsTest, TimeUpdateStopsWhenPlaybackPauses) {
  options_.timeupdate_interval_ms = 100;
  ToPlaying();
  Do([this] { player_->Pause(); });
  PullWithoutProgress();
  env_.FastForwardBy(absl::Milliseconds(1000));
  EXPECT_EQ(PullWithoutProgress(), Names{});
}

TEST_F(EventsTest, TimeUpdateResumesWhenPlaybackStartsAgain) {
  options_.timeupdate_interval_ms = 100;
  ToPlaying();
  Do([this] { player_->Pause(); });
  PullWithoutProgress();
  env_.FastForwardBy(absl::Milliseconds(500));
  Play();
  EXPECT_EQ(PullWithoutProgress(), (Names{"play", "playing", "result:ok"}));
  env_.FastForwardBy(absl::Milliseconds(100));
  EXPECT_EQ(PullWithoutProgress(), (Names{"timeupdate"}));
}

TEST_F(EventsTest, TimeUpdateReportsThePortPosition) {
  options_.timeupdate_interval_ms = 100;
  ToPlaying();
  Do([this] {
    Fake()->SetMediaTime(penumbra::MediaTime::FromMilliseconds(250));
  });
  env_.FastForwardBy(absl::Milliseconds(100));
  Pull();
  EXPECT_EQ(recorder_->StateOf("timeupdate").current_time, 0.25);
}

TEST_F(EventsTest, ProgressFiresEvery350MillisecondsWhileDataArrives) {
  Start();
  Open();
  Pull();
  Grant(test::kVideoTrack, 1);
  env_.FastForwardBy(absl::Milliseconds(349));
  EXPECT_EQ(Pull(), Names{});
  env_.FastForwardBy(absl::Milliseconds(1));
  EXPECT_EQ(Pull(), (Names{"progress"}));
  env_.FastForwardBy(absl::Milliseconds(350));
  EXPECT_EQ(Pull(), Names{});
  Grant(test::kVideoTrack, 1);
  env_.FastForwardBy(absl::Milliseconds(350));
  EXPECT_EQ(Pull(), (Names{"progress"}));
}

TEST_F(EventsTest, ProgressStopsAfterTheFetchCompletes) {
  Start("https://example.test/a.mp4", 600);
  Open();
  Preroll();
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  Pull();
  env_.FastForwardBy(absl::Milliseconds(2000));
  EXPECT_EQ(Pull(), Names{});
  EXPECT_EQ(player_->network_state(), NetworkState::kIdle);
}

TEST_F(EventsTest, StalledFiresOnceWhenNoDataArrivesForThreeSeconds) {
  Start();
  Open();
  Pull();
  source_->Hold();
  Grant(test::kVideoTrack, 1);
  env_.FastForwardBy(absl::Milliseconds(2999));
  EXPECT_EQ(Pull(), Names{});
  env_.FastForwardBy(absl::Milliseconds(1));
  EXPECT_EQ(Pull(), (Names{"stalled"}));
  env_.FastForwardBy(absl::Milliseconds(9000));
  EXPECT_EQ(Pull(), Names{});
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
}

TEST_F(EventsTest, StalledFiresAgainAfterDataResumed) {
  Start();
  Open();
  Pull();
  source_->Hold();
  Grant(test::kVideoTrack, 1);
  env_.FastForwardBy(absl::Milliseconds(3000));
  EXPECT_EQ(Pull(), (Names{"stalled"}));
  Do([this] { source_->Release(); });
  Pull();
  env_.FastForwardBy(absl::Milliseconds(3000));
  Pull();
  source_->Hold();
  Grant(test::kVideoTrack, 1);
  env_.FastForwardBy(absl::Milliseconds(6000));
  EXPECT_EQ(Pull(), (Names{"stalled"}));
}

TEST_F(EventsTest, StalledFiresWhenTheSourceNeverOpens) {
  Start();
  Pull();
  env_.FastForwardBy(absl::Milliseconds(3000));
  EXPECT_EQ(Pull(), (Names{"stalled"}));
}

TEST_F(EventsTest, NoStalledWhenTheBufferIsFullAndNothingIsRequested) {
  Start();
  Open();
  Preroll();
  PullWithoutProgress();
  env_.FastForwardBy(absl::Milliseconds(10000));
  EXPECT_EQ(PullWithoutProgress(), Names{});
}

TEST_F(EventsTest, PreloadNoneSuspendsBeforeFetchingUntilPlay) {
  Create();
  recorder_ = std::make_unique<Recorder>(*player_);
  Do([this] { player_->set_preload(Preload::kNone); });
  SetSrc("https://example.test/a.mp4");
  EXPECT_EQ(Pull(), (Names{"loadstart", "suspend"}));
  EXPECT_EQ(player_->network_state(), NetworkState::kIdle);
  EXPECT_FALSE(source_->opened);
  env_.FastForwardBy(absl::Milliseconds(10000));
  EXPECT_EQ(Pull(), Names{});
  Play();
  EXPECT_TRUE(source_->opened);
  EXPECT_EQ(player_->network_state(), NetworkState::kLoading);
  EXPECT_EQ(Pull(), (Names{"play", "waiting"}));
}

TEST_F(EventsTest, LeavingPreloadNoneStartsTheFetch) {
  Create();
  recorder_ = std::make_unique<Recorder>(*player_);
  Do([this] { player_->set_preload(Preload::kNone); });
  SetSrc("https://example.test/a.mp4");
  Pull();
  Do([this] { player_->set_preload(Preload::kMetadata); });
  EXPECT_TRUE(source_->opened);
  Open();
  EXPECT_EQ(Pull(), (Names{"durationchange", "resize", "loadedmetadata"}));
}

TEST_F(EventsTest, QueuedEventsCarryTheValuesOfTheMomentTheyFired) {
  ToPlaying();
  Do([this] {
    player_->Pause();
    EXPECT_TRUE(player_->set_current_time(2.0));
  });
  Pull();
  EXPECT_TRUE(recorder_->StateOf("pause").paused);
  EXPECT_FALSE(recorder_->StateOf("pause").seeking);
  EXPECT_TRUE(recorder_->StateOf("seeking").seeking);
  EXPECT_EQ(recorder_->StateOf("seeking").duration, 3.0);
}

TEST_F(EventsTest, DurationIsNanInEventsBeforeMetadata) {
  Start();
  Pull();
  EXPECT_TRUE(std::isnan(recorder_->StateOf("loadstart").duration));
}

TEST_F(EventsTest, CancellingASubscriptionStopsDelivery) {
  Create();
  int calls = 0;
  Subscription subscription = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++calls; });
  EXPECT_TRUE(subscription.active());
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(calls, 1);
  subscription.Cancel();
  EXPECT_FALSE(subscription.active());
  Do([this] { player_->Load(); });
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(EventsTest, DestroyingTheSubscriptionRemovesTheListener) {
  Create();
  int calls = 0;
  {
    const Subscription subscription = player_->AddEventListener<LoadStartEvent>(
        [&](const LoadStartEvent&) { ++calls; });
  }
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(calls, 0);
}

TEST_F(EventsTest, ASubscriptionCancelledAfterQueueingIsNotCalled) {
  Create();
  int calls = 0;
  Subscription subscription = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++calls; });
  SetSrc("https://example.test/a.mp4");
  subscription.Cancel();
  Dispatch();
  EXPECT_EQ(calls, 0);
}

TEST_F(EventsTest, ASubscriptionCanOutliveThePlayer) {
  Create();
  Subscription subscription =
      player_->AddEventListener<LoadStartEvent>([](const LoadStartEvent&) {});
  Destroy();
  EXPECT_TRUE(subscription.active());
  subscription.Cancel();
  EXPECT_FALSE(subscription.active());
}

TEST_F(EventsTest, SubscriptionsMoveWithoutCancelling) {
  Create();
  int calls = 0;
  Subscription first = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++calls; });
  Subscription second = std::move(first);
  EXPECT_TRUE(second.active());
  Subscription third;
  third = std::move(second);
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(calls, 1);
}

TEST_F(EventsTest, ListenersRunInRegistrationOrder) {
  Create();
  Names order;
  const Subscription first = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { order.emplace_back("first"); });
  const Subscription second = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { order.emplace_back("second"); });
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(order, (Names{"first", "second"}));
}

TEST_F(EventsTest, AListenerMayAddAnotherListenerForLaterEvents) {
  Create();
  struct State {
    Player* player = nullptr;
    int added_calls = 0;
    std::vector<Subscription> added;
  } state;
  state.player = player_.get();
  const Subscription first = player_->AddEventListener<LoadStartEvent>(
      [&state](const LoadStartEvent&) {
        if (state.added.empty()) {
          state.added.push_back(state.player->AddEventListener<LoadStartEvent>(
              [&state](const LoadStartEvent&) { ++state.added_calls; }));
        }
      });
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(state.added_calls, 0);
  Do([this] { player_->Load(); });
  Dispatch();
  EXPECT_EQ(state.added_calls, 1);
}

TEST_F(EventsTest, AListenerMayCancelALaterListenerOfTheSameEvent) {
  Create();
  int second_calls = 0;
  Subscription second;
  const Subscription first = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { second.Cancel(); });
  second = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++second_calls; });
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_EQ(second_calls, 0);
}

TEST_F(EventsTest, ListenersMayCallBackIntoThePlayer) {
  Create();
  const Subscription subscription = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { player_->Pause(); });
  SetSrc("https://example.test/a.mp4");
  Dispatch();
  EXPECT_TRUE(player_->paused());
}

TEST_F(EventsTest, NoEventIsDeliveredAfterThePlayerIsDestroyed) {
  Create();
  int calls = 0;
  const Subscription subscription = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++calls; });
  SetSrc("https://example.test/a.mp4");
  Destroy();
  EXPECT_EQ(calls, 0);
}

TEST_F(EventsTest, DestroyingThePlayerFromAListenerDropsTheRestOfTheQueue) {
  Create();
  int later = 0;
  const Subscription first = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { player_.reset(); });
  const Subscription second = player_->AddEventListener<LoadStartEvent>(
      [&](const LoadStartEvent&) { ++later; });
  const Subscription third = player_->AddEventListener<AbortEvent>(
      [&](const AbortEvent&) { ++later; });
  SetSrc("https://example.test/a.mp4");
  Do([this] { player_->DispatchEvents(); });
  EXPECT_EQ(player_, nullptr);
  EXPECT_EQ(later, 0);
}

TEST_F(EventsTest, EventNamesAreTheHtmlNames) {
  EXPECT_EQ(LoadStartEvent::kName, "loadstart");
  EXPECT_EQ(CanPlayThroughEvent::kName, "canplaythrough");
  EXPECT_EQ(DurationChangeEvent::kName, "durationchange");
  EXPECT_EQ(VolumeChangeEvent::kName, "volumechange");
  EXPECT_EQ(WarningEvent::kName, "warning");
  EXPECT_EQ(TimeUpdateEvent::kType, EventType::kTimeUpdate);
}

TEST_F(EventsTest, CreateRejectsATimeUpdateIntervalOutsideTheRange) {
  options_.timeupdate_interval_ms = 14;
  Create();
  EXPECT_EQ(player_, nullptr);
  options_.timeupdate_interval_ms = 251;
  Create();
  EXPECT_EQ(player_, nullptr);
  options_.timeupdate_interval_ms = 15;
  Create();
  EXPECT_NE(player_, nullptr);
}

}
}
