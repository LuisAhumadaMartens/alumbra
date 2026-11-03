#include "element_harness.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "event_trace.h"
#include "penumbra/interface/port_types.h"
#include "player_fixture.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/player/events.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"

namespace umbra::player::conformance {

template <typename Event>
void EventLog::Listen(Player& player) {
  subscriptions_.push_back(
      player.AddEventListener<Event>([this](const Event& event) {
        events_.push_back({std::string(Event::kName), event.state});
      }));
}

EventLog::EventLog(Player& player) {
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

std::vector<ObservedEvent> EventLog::Take() {
  std::vector<ObservedEvent> taken = std::move(events_);
  events_.clear();
  return taken;
}

void EventLog::Mark(const std::string& name) {
  events_.push_back({name, ElementState()});
}

std::vector<std::string> NamesOf(const std::vector<ObservedEvent>& events) {
  std::vector<std::string> names;
  names.reserve(events.size());
  for (const ObservedEvent& event : events) {
    names.push_back(event.name);
  }
  return names;
}

void ElementHarness::StartElement() {
  Create();
  log_ = std::make_unique<EventLog>(*player_);
}

void ElementHarness::BeginLoad(int64_t duration_ms) {
  SetSrc("https://example.test/media.mp4", duration_ms);
}

std::vector<ObservedEvent> ElementHarness::PullEvents() {
  Dispatch();
  return log_->Take();
}

std::vector<std::string> ElementHarness::Pull() {
  return NamesOf(PullEvents());
}

std::vector<std::string> ElementHarness::PullWithoutProgress() {
  std::vector<std::string> names = Pull();
  std::erase(names, "progress");
  return names;
}

void ElementHarness::PlayWithMarkers() {
  Do([this] {
    player_->Play([this](const PlayResult& result) {
      log_->Mark(result.code == PlayResultCode::kOk ? "result:ok"
                                                    : "result:rejected");
    });
  });
}

void ElementHarness::ReachMetadata() {
  StartElement();
  BeginLoad();
  Open();
  Pull();
}

void ElementHarness::ReachEnoughData() {
  ReachMetadata();
  Preroll();
  Pull();
}

void ElementHarness::ReachPlaying(int64_t duration_ms) {
  StartElement();
  BeginLoad(duration_ms);
  Open();
  Preroll();
  Pull();
  PlayWithMarkers();
  PortState(penumbra::PlayerState::kPresenting);
  Pull();
}

void ElementHarness::CompleteSeek() {
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
  PortState(penumbra::PlayerState::kReady);
}

void ElementHarness::FinishPlayback() {
  Grant(test::kVideoTrack);
  Grant(test::kAudioTrack);
}

double ElementHarness::CurrentTime() {
  double time = -1.0;
  Do([&] { time = player_->current_time(); });
  return time;
}

bool ElementHarness::RunStep(const TraceStep& step, std::string& error) {
  const std::string& action = step.action;
  double number = 0.0;
  if (!step.arguments.empty() &&
      !absl::SimpleAtod(step.arguments.front(), &number)) {
    error = absl::StrCat("step ", action, ": numeric argument expected");
    return false;
  }
  if (action == "set_src") {
    BeginLoad(static_cast<int64_t>(number));
  } else if (action == "set_src_empty") {
    SetSrc("");
  } else if (action == "open") {
    Open();
  } else if (action == "fail_open") {
    Do([this] {
      source_->FailOpen(test::DetailFor(base::ErrorCode::kNetworkAccessDenied));
    });
  } else if (action == "preroll") {
    Preroll();
  } else if (action == "presenting") {
    PortState(penumbra::PlayerState::kPresenting);
  } else if (action == "play") {
    PlayWithMarkers();
  } else if (action == "pause") {
    Do([this] { player_->Pause(); });
  } else if (action == "seek") {
    Do([this, number] { player_->set_current_time(number); });
  } else if (action == "complete_seek") {
    CompleteSeek();
  } else if (action == "finish_playback") {
    FinishPlayback();
  } else if (action == "load") {
    Do([this] { player_->Load(); });
  } else if (action == "set_rate") {
    Do([this, number] { player_->set_playback_rate(number); });
  } else if (action == "set_volume") {
    Do([this, number] { player_->set_volume(number); });
  } else if (action == "set_muted") {
    Do([this, number] { player_->set_muted(number != 0.0); });
  } else if (action == "set_loop") {
    Do([this, number] { player_->set_loop(number != 0.0); });
  } else if (action == "set_autoplay") {
    Do([this, number] { player_->set_autoplay(number != 0.0); });
  } else if (action == "advance") {
    env_.FastForwardBy(absl::Milliseconds(static_cast<int64_t>(number)));
  } else if (action == "observe") {
  } else {
    error = absl::StrCat("unknown trace action: ", action);
    return false;
  }
  return true;
}

}
