#include "umbra/pipeline/playback_state_machine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/playback_state.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline {
namespace {

constexpr absl::Duration kStopRetryDelay = absl::Milliseconds(10);

base::ErrorDetail MakeDetail(base::ErrorCode code,
                             std::map<std::string, std::string> context = {}) {
  const base::ErrorCatalogueEntry* entry = base::FindError(code);
  assert(entry != nullptr);
  return {code,
          entry->category,
          entry->severity,
          entry->retryable,
          std::move(context),
          nullptr};
}

base::ErrorDetail DetailForStatus(const absl::Status& status,
                                  base::ErrorCode fallback) {
  auto payload = base::GetErrorDetail(status);
  if (payload.ok() && payload->has_value()) {
    return **payload;
  }
  return MakeDetail(
      fallback,
      {{"port_status", std::string(absl::StatusCodeToString(status.code()))}});
}

base::ErrorCode CodeForCategory(penumbra::PortErrorCategory category) {
  switch (category) {
    case penumbra::PortErrorCategory::kDecode:
      return base::ErrorCode::kMediaCorrupt;
    case penumbra::PortErrorCategory::kCapability:
      return base::ErrorCode::kCodecUnsupported;
    case penumbra::PortErrorCategory::kOutput:
    case penumbra::PortErrorCategory::kPlatform:
      return base::ErrorCode::kPlayerInternal;
  }
  return base::ErrorCode::kPlayerInternal;
}

base::ErrorDetail DetailForCreateFailure(const absl::Status& status) {
  const bool unsupported = status.code() == absl::StatusCode::kUnimplemented ||
                           status.code() == absl::StatusCode::kInvalidArgument;
  return DetailForStatus(
      status, unsupported ? base::ErrorCode::kCodecUnsupported
                          : base::ErrorCode::kDecoderInitializationFailed);
}

MediaErrorCode MediaErrorFor(const base::ErrorDetail& detail) {
  const base::ErrorCatalogueEntry* entry = base::FindError(detail.code);
  if (entry == nullptr || entry->media_error < 1 || entry->media_error > 4) {
    return MediaErrorCode::kDecode;
  }
  return static_cast<MediaErrorCode>(entry->media_error);
}

bool SameRate(double a, double b) { return !(a < b) && !(a > b); }

struct StopHolder {
  std::unique_ptr<penumbra::Player> player;
  std::shared_ptr<penumbra::CallbackDispatcher> dispatcher;
};

void RetryStop(const std::shared_ptr<penumbra::ControlTaskRunner>& control,
               std::shared_ptr<StopHolder> holder) {
  if (holder->player->Stop().ok()) {
    holder->dispatcher->Stop();
    return;
  }
  control
      ->PostTask(
          [control, holder = std::move(holder)]() mutable {
            RetryStop(control, std::move(holder));
          },
          kStopRetryDelay)
      .IgnoreError();
}

}

class PlaybackStateMachine::PortReceiver final
    : public penumbra::PlayerCallbacks {
 public:
  PortReceiver(std::shared_ptr<Anchor> machine,
               std::shared_ptr<penumbra::ControlTaskRunner> control,
               uint64_t generation)
      : machine_(std::move(machine)),
        control_(std::move(control)),
        generation_(generation) {}

  absl::Status OnEvent(penumbra::PlayerEvent event) override {
    return control_->PostTask(
        [machine = machine_, generation = generation_,
         event = std::move(event)]() mutable {
          PlaybackStateMachine* target = machine->machine;
          if (target == nullptr || target->port_generation_ != generation) {
            return;
          }
          target->OnPortEvent(std::move(event));
        },
        absl::ZeroDuration());
  }

 private:
  std::shared_ptr<Anchor> machine_;
  std::shared_ptr<penumbra::ControlTaskRunner> control_;
  uint64_t generation_;
};

class PlaybackStateMachine::SourceReceiver final : public SampleSink {
 public:
  SourceReceiver(std::shared_ptr<Anchor> machine, uint64_t generation)
      : machine_(std::move(machine)), generation_(generation) {}

  void OnOpened(SourceDescription description) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceOpened(std::move(description));
    }
  }

  void OnOpenFailed(base::ErrorDetail detail) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceFailed(std::move(detail));
    }
  }

  void OnDurationChanged(penumbra::MediaTime duration) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceDuration(duration);
    }
  }

  void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                 std::vector<penumbra::EncodedSample> samples) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceSamples(ticket, track_id, std::move(samples));
    }
  }

  void OnEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceEndOfStream(ticket, track_id);
    }
  }

  void OnSourceError(base::ErrorDetail detail) override {
    if (PlaybackStateMachine* machine = Live()) {
      machine->OnSourceError(std::move(detail));
    }
  }

 private:
  [[nodiscard]] PlaybackStateMachine* Live() const {
    if (machine_->machine == nullptr ||
        machine_->machine->source_generation_ != generation_) {
      return nullptr;
    }
    return machine_->machine;
  }

  std::shared_ptr<Anchor> machine_;
  uint64_t generation_;
};

absl::Status PlaybackStateMachine::ValidateConfig(const Config& config) {
  if (config.preroll_ms < kMinPrerollMs || config.preroll_ms > kMaxPrerollMs) {
    return absl::InvalidArgumentError("playback.preroll_ms must be 0 to 10000");
  }
  if (config.max_recreate_attempts < 0) {
    return absl::InvalidArgumentError(
        "max_recreate_attempts must not be negative");
  }
  return absl::OkStatus();
}

PlaybackStateMachine::PlaybackStateMachine(
    penumbra::PlayerFactory& factory,
    std::shared_ptr<penumbra::ControlTaskRunner> control, Config config)
    : factory_(factory),
      control_(std::move(control)),
      config_(std::move(config)),
      tracker_(config_.on_transition) {
  assert(control_ != nullptr);
  assert(ValidateConfig(config_).ok());
}

PlaybackStateMachine::~PlaybackStateMachine() { Shutdown(); }

void PlaybackStateMachine::Shutdown() {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return;
  }
  shut_down_ = true;
  TearDownPipeline();
  anchor_->machine = nullptr;
  pending_plays_.clear();
  static_cast<void>(events_.Drain());
}

MediaElementState PlaybackStateMachine::Snapshot() const {
  return {.network = network_,
          .ready = ready_,
          .paused = paused_,
          .seeking = seeking_,
          .ended = playback_ended_,
          .current_time = position_,
          .duration = duration_,
          .error = error_.has_value() ? error_->code : MediaErrorCode::kNone};
}

penumbra::MediaTime PlaybackStateMachine::CurrentTime() {
  assert(sequence_.CalledOnValidSequence());
  RefreshPosition();
  return position_;
}

void PlaybackStateMachine::Fire(MediaEventType type) {
  events_.Push(MediaEvent{.type = type, .state = Snapshot()});
}

void PlaybackStateMachine::PushCompletion(
    PlayCallback callback, PlayResultCode code,
    std::optional<base::ErrorDetail> detail) {
  events_.Push(
      PlayCompletion{.callback = std::move(callback),
                     .result = {.code = code, .detail = std::move(detail)}});
}

void PlaybackStateMachine::NotifyAboutPlaying() {
  std::vector<PlayCallback> promises = std::move(pending_plays_);
  pending_plays_.clear();
  Fire(MediaEventType::kPlaying);
  for (auto& promise : promises) {
    PushCompletion(std::move(promise), PlayResultCode::kOk);
  }
}

void PlaybackStateMachine::CompletePendingPlays(
    PlayResultCode code, const std::optional<base::ErrorDetail>& detail) {
  std::vector<PlayCallback> promises = std::move(pending_plays_);
  pending_plays_.clear();
  for (auto& promise : promises) {
    PushCompletion(std::move(promise), code, detail);
  }
}

bool PlaybackStateMachine::PlaybackAllowed() const {
  return !config_.allow_playback || config_.allow_playback();
}

void PlaybackStateMachine::Load(std::unique_ptr<SampleSource> source) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return;
  }
  AbortAndReset();
  can_autoplay_ = true;
  recreate_attempts_ = 0;
  if (source != nullptr) {
    SelectResource(std::move(source));
  }
}

void PlaybackStateMachine::AbortAndReset() {
  events_.DropEvents();
  RefreshPosition();
  const NetworkState previous = network_;
  const penumbra::MediaTime previous_position = position_;
  TearDownPipeline();
  error_.reset();
  ready_ = ReadyState::kHaveNothing;
  opened_ = false;
  prerolled_ = false;
  loaded_data_fired_ = false;
  fetch_complete_ = false;
  seeking_ = false;
  playback_ended_ = false;
  underflowed_ = false;
  port_state_ = PortState::kNone;
  applied_rate_ = 0.0;
  position_ = penumbra::MediaTime::Zero();
  duration_ = penumbra::MediaTime::Unknown();
  description_ = {};
  default_start_position_.reset();
  if (previous != NetworkState::kEmpty && !paused_) {
    paused_ = true;
    CompletePendingPlays(PlayResultCode::kAbort, std::nullopt);
  }
  const bool rate_changed = !SameRate(playback_rate_, default_rate_);
  playback_rate_ = default_rate_;
  network_ = NetworkState::kEmpty;
  UpdateState();
  if (previous == NetworkState::kLoading || previous == NetworkState::kIdle) {
    Fire(MediaEventType::kAbort);
  }
  if (previous != NetworkState::kEmpty) {
    Fire(MediaEventType::kEmptied);
    if (previous_position != penumbra::MediaTime::Zero()) {
      Fire(MediaEventType::kTimeUpdate);
    }
  }
  if (rate_changed) {
    Fire(MediaEventType::kRateChange);
  }
}

void PlaybackStateMachine::SelectResource(
    std::unique_ptr<SampleSource> source) {
  network_ = NetworkState::kLoading;
  Fire(MediaEventType::kLoadStart);
  source_ = std::move(source);
  ++source_generation_;
  UpdateState();
  source_->Open(std::make_shared<SourceReceiver>(anchor_, source_generation_));
}

void PlaybackStateMachine::Play(PlayCallback done) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return;
  }
  if (!PlaybackAllowed()) {
    PushCompletion(std::move(done), PlayResultCode::kNotAllowed);
    return;
  }
  if (error_.has_value() && error_->code == MediaErrorCode::kSrcNotSupported) {
    PushCompletion(std::move(done), PlayResultCode::kNotSupported,
                   error_->detail);
    return;
  }
  pending_plays_.push_back(std::move(done));
  InternalPlay();
}

void PlaybackStateMachine::InternalPlay() {
  RefreshPosition();
  if (playback_ended_) {
    RunSeek(penumbra::MediaTime::Zero());
  }
  if (paused_) {
    paused_ = false;
    Fire(MediaEventType::kPlay);
    if (ready_ <= ReadyState::kHaveCurrentData) {
      Fire(MediaEventType::kWaiting);
    } else {
      NotifyAboutPlaying();
    }
  } else if (ready_ >= ReadyState::kHaveFutureData) {
    CompletePendingPlays(PlayResultCode::kOk, std::nullopt);
  }
  can_autoplay_ = false;
  SyncPortRateOrFail();
  UpdateState();
}

void PlaybackStateMachine::Pause() {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return;
  }
  InternalPause();
}

void PlaybackStateMachine::InternalPause() {
  RefreshPosition();
  can_autoplay_ = false;
  if (!paused_) {
    paused_ = true;
    Fire(MediaEventType::kTimeUpdate);
    Fire(MediaEventType::kPause);
    CompletePendingPlays(PlayResultCode::kAbort, std::nullopt);
  }
  SyncPortRateOrFail();
  UpdateState();
}

absl::Status PlaybackStateMachine::Seek(penumbra::MediaTime position) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return absl::FailedPreconditionError("player is shut down");
  }
  if (!position.is_finite()) {
    return absl::InvalidArgumentError("seek position must be finite");
  }
  if (error_.has_value()) {
    return absl::FailedPreconditionError("playback has failed");
  }
  if (ready_ == ReadyState::kHaveNothing) {
    default_start_position_ = position;
    return absl::OkStatus();
  }
  RunSeek(position);
  return absl::OkStatus();
}

void PlaybackStateMachine::RunSeek(penumbra::MediaTime target) {
  target = std::max(target, penumbra::MediaTime::Zero());
  if (duration_.is_finite()) {
    target = std::min(target, duration_);
  }
  seeking_ = true;
  playback_ended_ = false;
  position_ = target;
  fetch_complete_ = false;
  if (network_ == NetworkState::kIdle) {
    network_ = NetworkState::kLoading;
  }
  Fire(MediaEventType::kSeeking);
  if (player_ != nullptr) {
    const absl::Status status = IssueSeek(target);
    if (!status.ok()) {
      HandlePortFailure({.status = status,
                         .category = penumbra::PortErrorCategory::kPlatform,
                         .recovery = penumbra::RecoveryHint::kRecreate});
      return;
    }
  }
  Reevaluate();
}

void PlaybackStateMachine::CompleteSeek() {
  seeking_ = false;
  Fire(MediaEventType::kTimeUpdate);
  Fire(MediaEventType::kSeeked);
}

void PlaybackStateMachine::HandleEndOfPlayback() {
  if (seeking_ || playback_ended_) {
    return;
  }
  RefreshPosition();
  if (duration_.is_finite()) {
    position_ = duration_;
  }
  if (loop_) {
    RunSeek(penumbra::MediaTime::Zero());
    return;
  }
  playback_ended_ = true;
  Fire(MediaEventType::kTimeUpdate);
  if (!paused_) {
    paused_ = true;
    Fire(MediaEventType::kPause);
    CompletePendingPlays(PlayResultCode::kAbort, std::nullopt);
  }
  Fire(MediaEventType::kEnded);
  SyncPortRateOrFail();
  UpdateState();
}

absl::Status PlaybackStateMachine::SetPlaybackRate(double rate) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return absl::FailedPreconditionError("player is shut down");
  }
  if (!std::isfinite(rate) || rate < 0.0) {
    return absl::InvalidArgumentError("playback rate must be finite and >= 0");
  }
  if (SameRate(rate, playback_rate_)) {
    return absl::OkStatus();
  }
  const double previous = playback_rate_;
  playback_rate_ = rate;
  absl::Status status = SyncPortRate();
  if (!status.ok()) {
    playback_rate_ = previous;
    return status;
  }
  Fire(MediaEventType::kRateChange);
  return absl::OkStatus();
}

absl::Status PlaybackStateMachine::SetDefaultPlaybackRate(double rate) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return absl::FailedPreconditionError("player is shut down");
  }
  if (!std::isfinite(rate) || rate < 0.0) {
    return absl::InvalidArgumentError("playback rate must be finite and >= 0");
  }
  if (!SameRate(rate, default_rate_)) {
    default_rate_ = rate;
    Fire(MediaEventType::kRateChange);
  }
  return absl::OkStatus();
}

double PlaybackStateMachine::EffectiveVolume() const {
  return muted_ ? 0.0 : volume_;
}

absl::Status PlaybackStateMachine::SetVolume(double volume) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return absl::FailedPreconditionError("player is shut down");
  }
  if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
    return absl::InvalidArgumentError("volume must be in [0, 1]");
  }
  if (SameRate(volume, volume_)) {
    return absl::OkStatus();
  }
  const double previous = volume_;
  volume_ = volume;
  if (player_ != nullptr) {
    const absl::Status status = player_->SetVolume(EffectiveVolume());
    if (!status.ok()) {
      volume_ = previous;
      return status;
    }
  }
  Fire(MediaEventType::kVolumeChange);
  return absl::OkStatus();
}

absl::Status PlaybackStateMachine::SetMuted(bool muted) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_) {
    return absl::FailedPreconditionError("player is shut down");
  }
  if (muted == muted_) {
    return absl::OkStatus();
  }
  const bool previous = muted_;
  muted_ = muted;
  if (player_ != nullptr) {
    const absl::Status status = player_->SetVolume(EffectiveVolume());
    if (!status.ok()) {
      muted_ = previous;
      return status;
    }
  }
  Fire(MediaEventType::kVolumeChange);
  return absl::OkStatus();
}

bool PlaybackStateMachine::PotentiallyPlaying() const {
  return !paused_ && ready_ >= ReadyState::kHaveFutureData &&
         !playback_ended_ && !error_.has_value();
}

void PlaybackStateMachine::SetReadyState(ReadyState next) {
  const ReadyState previous = ready_;
  if (next == previous) {
    return;
  }
  const bool was_potentially_playing = PotentiallyPlaying();
  ready_ = next;
  if (next >= ReadyState::kHaveFutureData) {
    prerolled_ = true;
  }
  if (previous == ReadyState::kHaveNothing &&
      next >= ReadyState::kHaveMetadata) {
    Fire(MediaEventType::kLoadedMetadata);
  }
  if (previous <= ReadyState::kHaveMetadata &&
      next >= ReadyState::kHaveCurrentData && !loaded_data_fired_) {
    loaded_data_fired_ = true;
    Fire(MediaEventType::kLoadedData);
  }
  if (previous >= ReadyState::kHaveFutureData &&
      next <= ReadyState::kHaveCurrentData && was_potentially_playing) {
    Fire(MediaEventType::kTimeUpdate);
    Fire(MediaEventType::kWaiting);
  }
  if (previous <= ReadyState::kHaveCurrentData &&
      next >= ReadyState::kHaveFutureData) {
    Fire(MediaEventType::kCanPlay);
    if (!paused_) {
      NotifyAboutPlaying();
    }
  }
  if (next == ReadyState::kHaveEnoughData) {
    Fire(MediaEventType::kCanPlayThrough);
    if (paused_ && can_autoplay_ && autoplay_ && PlaybackAllowed()) {
      paused_ = false;
      Fire(MediaEventType::kPlay);
      NotifyAboutPlaying();
    }
  }
}

ReadyState PlaybackStateMachine::ComputeReadyState() const {
  if (!opened_) {
    return ReadyState::kHaveNothing;
  }
  if (port_state_ == PortState::kNone) {
    return ReadyState::kHaveMetadata;
  }
  if (underflowed_) {
    return ReadyState::kHaveCurrentData;
  }
  penumbra::TimeDelta ahead = penumbra::TimeDelta::Infinite();
  for (const TrackState& track : tracks_) {
    if (track.eos_written) {
      continue;
    }
    const penumbra::TimeDelta buffered = track.buffered_end.is_finite()
                                             ? track.buffered_end - position_
                                             : penumbra::TimeDelta::Zero();
    ahead = std::min(ahead, buffered);
  }
  ahead = std::max(ahead, penumbra::TimeDelta::Zero());
  const auto future = penumbra::TimeDelta::FromMilliseconds(config_.preroll_ms);
  const auto enough =
      penumbra::TimeDelta::FromMilliseconds(int64_t{config_.preroll_ms} * 2);
  if (ahead >= enough) {
    return ReadyState::kHaveEnoughData;
  }
  if (ahead >= future) {
    return ReadyState::kHaveFutureData;
  }
  return ReadyState::kHaveCurrentData;
}

void PlaybackStateMachine::Reevaluate() {
  if (shut_down_ || !opened_ || error_.has_value()) {
    return;
  }
  RefreshPosition();
  const ReadyState target = ComputeReadyState();
  if (seeking_ && target >= ReadyState::kHaveCurrentData) {
    SetReadyState(std::max(ready_, ReadyState::kHaveCurrentData));
    CompleteSeek();
  }
  SetReadyState(target);
  SyncPortRateOrFail();
  UpdateState();
}

void PlaybackStateMachine::RefreshPosition() {
  if (player_ == nullptr || playback_ended_ || seeking_ || error_.has_value()) {
    return;
  }
  const auto snapshot = player_->GetSnapshot();
  if (snapshot.ok() && snapshot->media_time.is_finite()) {
    position_ = std::max(snapshot->media_time, penumbra::MediaTime::Zero());
  }
}

PlaybackState PlaybackStateMachine::DeriveState() const {
  if (error_.has_value()) {
    return PlaybackState::kError;
  }
  if (network_ == NetworkState::kEmpty) {
    return PlaybackState::kIdle;
  }
  if (ready_ == ReadyState::kHaveNothing) {
    return PlaybackState::kLoading;
  }
  if (seeking_) {
    return PlaybackState::kSeeking;
  }
  if (!prerolled_) {
    return PlaybackState::kPrerolling;
  }
  if (playback_ended_) {
    return PlaybackState::kEnded;
  }
  if (ready_ < ReadyState::kHaveFutureData) {
    return PlaybackState::kBuffering;
  }
  if (!paused_) {
    return PlaybackState::kPlaying;
  }
  const PlaybackState current = tracker_.GetState();
  return current == PlaybackState::kPrerolling ||
                 current == PlaybackState::kReady
             ? PlaybackState::kReady
             : PlaybackState::kPaused;
}

void PlaybackStateMachine::UpdateState() {
  const PlaybackState next = DeriveState();
  if (next != tracker_.GetState()) {
    tracker_.Transition(next);
  }
}

double PlaybackStateMachine::DesiredPortRate() const {
  if (paused_ || seeking_ || playback_ended_ || error_.has_value() ||
      ready_ < ReadyState::kHaveFutureData) {
    return 0.0;
  }
  return playback_rate_;
}

absl::Status PlaybackStateMachine::SyncPortRate() {
  if (player_ == nullptr) {
    return absl::OkStatus();
  }
  const double desired = DesiredPortRate();
  if (SameRate(desired, applied_rate_)) {
    return absl::OkStatus();
  }
  absl::Status status = player_->SetPlaybackRate(desired);
  if (!status.ok()) {
    return status;
  }
  applied_rate_ = desired;
  rate_changed_since_presenting_ = true;
  return absl::OkStatus();
}

void PlaybackStateMachine::SyncPortRateOrFail() {
  const absl::Status status = SyncPortRate();
  if (!status.ok()) {
    HandlePortFailure({.status = status,
                       .category = penumbra::PortErrorCategory::kPlatform,
                       .recovery = penumbra::RecoveryHint::kFatal});
  }
}

absl::Status PlaybackStateMachine::CreatePort() {
  penumbra::PlayerConfig config;
  config.streams = description_.streams;
  config.initial_ticket = ticket_ + 1;
  ++port_generation_;
  auto receiver =
      std::make_shared<PortReceiver>(anchor_, control_, port_generation_);
  auto dispatcher = std::make_shared<penumbra::CallbackDispatcher>(
      config.initial_ticket, control_, std::move(receiver));
  const penumbra::SeekTicket initial = config.initial_ticket;
  auto created = factory_.CreatePlayer(std::move(config), dispatcher);
  if (!created.ok()) {
    dispatcher->Stop();
    return created.status();
  }
  player_ = *std::move(created);
  dispatcher_ = std::move(dispatcher);
  ticket_ = initial;
  applied_rate_ = 0.0;
  rate_changed_since_presenting_ = true;
  port_state_ = PortState::kNone;
  absl::Status status = player_->SetPlaybackRate(0.0);
  if (status.ok()) {
    status = player_->SetVolume(EffectiveVolume());
  }
  if (!status.ok()) {
    StopPort();
  }
  return status;
}

absl::Status PlaybackStateMachine::IssueSeek(penumbra::MediaTime position) {
  const penumbra::SeekTicket next = ticket_ + 1;
  absl::Status status = dispatcher_->AdvanceTicket(next);
  if (!status.ok()) {
    return status;
  }
  ticket_ = next;
  status = player_->Seek(next, position);
  if (!status.ok()) {
    return status;
  }
  ResetTracks(position);
  port_state_ = PortState::kNone;
  underflowed_ = false;
  if (source_ != nullptr) {
    source_->Seek(next, position);
  }
  return absl::OkStatus();
}

void PlaybackStateMachine::ResetTracks(penumbra::MediaTime position) {
  for (TrackState& track : tracks_) {
    track.buffered_end = position;
    track.eos_written = false;
    track.grant = 0;
  }
}

void PlaybackStateMachine::RebuildPort() {
  RefreshPosition();
  StopPort();
  ++port_generation_;
  absl::Status status = CreatePort();
  if (!status.ok()) {
    FailFatal(DetailForCreateFailure(status));
    return;
  }
  status = IssueSeek(position_);
  if (!status.ok()) {
    HandlePortFailure({.status = status,
                       .category = penumbra::PortErrorCategory::kPlatform,
                       .recovery = penumbra::RecoveryHint::kRecreate});
    return;
  }
  Reevaluate();
}

void PlaybackStateMachine::StopPort() {
  if (player_ == nullptr) {
    return;
  }
  auto player = std::move(player_);
  auto dispatcher = std::move(dispatcher_);
  if (player->Stop().ok()) {
    dispatcher->Stop();
    return;
  }
  auto holder = std::make_shared<StopHolder>(StopHolder{
      .player = std::move(player), .dispatcher = std::move(dispatcher)});
  control_
      ->PostTask(
          [control = control_, holder = std::move(holder)]() mutable {
            RetryStop(control, std::move(holder));
          },
          kStopRetryDelay)
      .IgnoreError();
}

void PlaybackStateMachine::TearDownPipeline() {
  ++source_generation_;
  ++port_generation_;
  if (source_ != nullptr) {
    source_->Close();
    control_
        ->PostTask(
            [released = std::move(source_)]() mutable { released.reset(); },
            absl::ZeroDuration())
        .IgnoreError();
  }
  StopPort();
  tracks_.clear();
  port_state_ = PortState::kNone;
}

PlaybackStateMachine::TrackState* PlaybackStateMachine::FindTrack(
    uint32_t track_id) {
  auto it = std::ranges::find_if(tracks_, [track_id](const TrackState& track) {
    return track.track_id == track_id;
  });
  return it == tracks_.end() ? nullptr : &*it;
}

void PlaybackStateMachine::OnSourceOpened(SourceDescription description) {
  assert(sequence_.CalledOnValidSequence());
  if (opened_ || error_.has_value() || network_ != NetworkState::kLoading) {
    return;
  }
  std::vector<penumbra::StreamConfig> usable;
  for (auto& stream : description.streams) {
    if (stream.info.type() != penumbra::StreamType::kText) {
      usable.push_back(std::move(stream));
    }
  }
  if (usable.empty()) {
    FailFatal(MakeDetail(base::ErrorCode::kCodecUnsupported,
                         {{"reason", "no playable stream"}}));
    return;
  }
  description_.streams = std::move(usable);
  description_.duration = description.duration;
  const absl::Status status = CreatePort();
  if (!status.ok()) {
    FailFatal(DetailForCreateFailure(status));
    return;
  }
  duration_ = description.duration;
  for (const auto& stream : description_.streams) {
    tracks_.push_back({.track_id = stream.info.track_id,
                       .buffered_end = position_,
                       .eos_written = false,
                       .grant = 0});
  }
  opened_ = true;
  if (duration_ != penumbra::MediaTime::Unknown()) {
    Fire(MediaEventType::kDurationChange);
  }
  SetReadyState(ReadyState::kHaveMetadata);
  UpdateState();
  if (default_start_position_.has_value()) {
    const penumbra::MediaTime start = *default_start_position_;
    default_start_position_.reset();
    RunSeek(start);
    return;
  }
  Reevaluate();
}

void PlaybackStateMachine::OnSourceFailed(base::ErrorDetail detail) {
  assert(sequence_.CalledOnValidSequence());
  if (opened_ || network_ != NetworkState::kLoading) {
    return;
  }
  FailFatal(std::move(detail));
}

void PlaybackStateMachine::OnSourceDuration(penumbra::MediaTime duration) {
  assert(sequence_.CalledOnValidSequence());
  if (!opened_ || duration == duration_) {
    return;
  }
  duration_ = duration;
  Fire(MediaEventType::kDurationChange);
  if (duration_.is_finite() && position_ > duration_) {
    RunSeek(duration_);
  }
}

void PlaybackStateMachine::OnSourceSamples(
    penumbra::SeekTicket ticket, uint32_t track_id,
    std::vector<penumbra::EncodedSample> samples) {
  assert(sequence_.CalledOnValidSequence());
  if (ticket != ticket_ || player_ == nullptr || samples.empty()) {
    return;
  }
  TrackState* track = FindTrack(track_id);
  if (track == nullptr || track->grant == 0 || track->eos_written) {
    return;
  }
  const absl::Status status = player_->WriteSamples(
      ticket_, track_id, std::span<const penumbra::EncodedSample>(samples));
  if (!status.ok()) {
    HandlePortFailure(
        {.status = status,
         .category = penumbra::PortErrorCategory::kPlatform,
         .recovery = status.code() == absl::StatusCode::kUnimplemented
                         ? penumbra::RecoveryHint::kRecreate
                         : penumbra::RecoveryHint::kFatal});
    return;
  }
  track->grant = 0;
  underflowed_ = false;
  for (const auto& sample : samples) {
    const auto start =
        penumbra::ToMediaTime(sample.pts, penumbra::Rounding::kNearest);
    if (!start.has_value() || !start->is_finite()) {
      continue;
    }
    penumbra::MediaTime end = *start;
    const auto length =
        penumbra::ToTimeDelta(sample.duration, penumbra::Rounding::kNearest);
    if (length.has_value() && length->is_finite()) {
      end = end + *length;
    }
    if (!track->buffered_end.is_finite() || end > track->buffered_end) {
      track->buffered_end = end;
    }
  }
  Reevaluate();
}

void PlaybackStateMachine::OnSourceEndOfStream(penumbra::SeekTicket ticket,
                                               uint32_t track_id) {
  assert(sequence_.CalledOnValidSequence());
  if (ticket != ticket_ || player_ == nullptr) {
    return;
  }
  TrackState* track = FindTrack(track_id);
  if (track == nullptr || track->grant == 0 || track->eos_written) {
    return;
  }
  const absl::Status status = player_->EndOfStream(ticket_, track_id);
  if (!status.ok()) {
    HandlePortFailure({.status = status,
                       .category = penumbra::PortErrorCategory::kPlatform,
                       .recovery = penumbra::RecoveryHint::kFatal});
    return;
  }
  track->grant = 0;
  track->eos_written = true;
  const bool all_written = std::ranges::all_of(
      tracks_,
      [](const TrackState& candidate) { return candidate.eos_written; });
  if (all_written && !fetch_complete_) {
    fetch_complete_ = true;
    Fire(MediaEventType::kProgress);
    network_ = NetworkState::kIdle;
    Fire(MediaEventType::kSuspend);
  }
  Reevaluate();
}

void PlaybackStateMachine::OnSourceError(base::ErrorDetail detail) {
  assert(sequence_.CalledOnValidSequence());
  if (detail.severity == base::ErrorSeverity::kFatal) {
    FailFatal(std::move(detail));
  }
}

void PlaybackStateMachine::OnPortEvent(penumbra::PlayerEvent event) {
  assert(sequence_.CalledOnValidSequence());
  if (shut_down_ || player_ == nullptr || event.ticket != ticket_) {
    return;
  }
  if (const auto* request = std::get_if<penumbra::NeedsData>(&event.detail)) {
    OnNeedsData(*request);
  } else if (const auto* change =
                 std::get_if<penumbra::StateChanged>(&event.detail)) {
    OnPortState(*change);
  } else if (const auto* failure =
                 std::get_if<penumbra::PortError>(&event.detail)) {
    HandlePortFailure(*failure);
  }
}

void PlaybackStateMachine::OnNeedsData(const penumbra::NeedsData& request) {
  TrackState* track = FindTrack(request.track_id);
  if (track == nullptr || track->eos_written || source_ == nullptr) {
    return;
  }
  track->grant = request.max_samples;
  source_->RequestSamples(ticket_, request.track_id, request.max_samples);
}

void PlaybackStateMachine::OnPortState(const penumbra::StateChanged& change) {
  if (change.track_id.has_value()) {
    return;
  }
  switch (change.state) {
    case penumbra::PlayerState::kReady:
      if (port_state_ == PortState::kPresenting && applied_rate_ > 0.0 &&
          !rate_changed_since_presenting_ && !paused_) {
        underflowed_ = true;
      }
      if (port_state_ != PortState::kEnded) {
        port_state_ = PortState::kReady;
      }
      break;
    case penumbra::PlayerState::kPresenting:
      port_state_ = PortState::kPresenting;
      rate_changed_since_presenting_ = false;
      recreate_attempts_ = 0;
      break;
    case penumbra::PlayerState::kEndOfStream:
      port_state_ = PortState::kEnded;
      HandleEndOfPlayback();
      return;
  }
  Reevaluate();
}

void PlaybackStateMachine::HandlePortFailure(const penumbra::PortError& error) {
  switch (error.recovery) {
    case penumbra::RecoveryHint::kTransient:
      return;
    case penumbra::RecoveryHint::kRecreate:
      if (player_ == nullptr || shut_down_ || error_.has_value()) {
        return;
      }
      if (++recreate_attempts_ > config_.max_recreate_attempts) {
        FailFatal(
            DetailForStatus(error.status, CodeForCategory(error.category)));
        return;
      }
      RebuildPort();
      return;
    case penumbra::RecoveryHint::kFatal:
      FailFatal(DetailForStatus(error.status, CodeForCategory(error.category)));
      return;
  }
}

void PlaybackStateMachine::FailFatal(base::ErrorDetail detail) {
  if (shut_down_ || error_.has_value()) {
    return;
  }
  const bool before_metadata = ready_ == ReadyState::kHaveNothing;
  const MediaErrorCode code = before_metadata ? MediaErrorCode::kSrcNotSupported
                                              : MediaErrorFor(detail);
  TearDownPipeline();
  error_ = MediaError{.code = code, .detail = detail};
  network_ = before_metadata ? NetworkState::kNoSource : NetworkState::kIdle;
  Fire(MediaEventType::kError);
  if (before_metadata) {
    CompletePendingPlays(PlayResultCode::kNotSupported, detail);
  }
  UpdateState();
}

}
