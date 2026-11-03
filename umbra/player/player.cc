#include "umbra/player/player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/observability/metrics.h"
#include "umbra/observability/statistics_model.h"
#include "umbra/pipeline/event_queue.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/playback_state_machine.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/can_play_type.h"
#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player_access.h"
#include "umbra/player/statistics.h"
#include "umbra/player/text_track.h"
#include "umbra/player/time_ranges.h"

namespace umbra::player {
namespace {

constexpr double kMicrosecondsPerSecond = 1e6;
constexpr double kMaxMicroseconds = 9.0e18;

double ToSeconds(penumbra::MediaTime time) {
  if (time.is_unknown()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (time.is_infinite()) {
    return time > penumbra::MediaTime::Zero()
               ? std::numeric_limits<double>::infinity()
               : -std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(time.InMicroseconds()) / kMicrosecondsPerSecond;
}

penumbra::MediaTime FromSeconds(double seconds) {
  const double micros = std::clamp(seconds * kMicrosecondsPerSecond,
                                   -kMaxMicroseconds, kMaxMicroseconds);
  return penumbra::MediaTime::FromMicroseconds(
      static_cast<int64_t>(std::llround(micros)));
}

MediaErrorCode ToPublic(pipeline::MediaErrorCode code) {
  switch (code) {
    case pipeline::MediaErrorCode::kAborted:
      return MediaErrorCode::kAborted;
    case pipeline::MediaErrorCode::kNetwork:
      return MediaErrorCode::kNetwork;
    case pipeline::MediaErrorCode::kNone:
    case pipeline::MediaErrorCode::kDecode:
      return MediaErrorCode::kDecode;
    case pipeline::MediaErrorCode::kSrcNotSupported:
      return MediaErrorCode::kSrcNotSupported;
  }
  return MediaErrorCode::kDecode;
}

class UnsupportedSource final : public pipeline::SampleSource {
 public:
  void Open(std::shared_ptr<pipeline::SampleSink> sink) override {
    const base::ErrorCatalogueEntry* entry =
        base::FindError(base::ErrorCode::kCodecUnsupported);
    sink->OnOpenFailed({entry->code,
                        entry->category,
                        entry->severity,
                        entry->retryable,
                        {{"reason", "no source handles this resource"}},
                        nullptr});
  }
  void RequestSamples([[maybe_unused]] penumbra::SeekTicket ticket,
                      [[maybe_unused]] uint32_t track_id,
                      [[maybe_unused]] size_t max_samples) override {}
  [[nodiscard]] pipeline::SeekPoint ResolveSeek(
      penumbra::MediaTime position, [[maybe_unused]] pipeline::SeekMode mode,
      [[maybe_unused]] pipeline::FastSeekSnap snap) const override {
    return {.presentation_time = position, .decode_time = position};
  }
  void Seek([[maybe_unused]] penumbra::SeekTicket ticket,
            [[maybe_unused]] penumbra::MediaTime position) override {}
  void Close() override {}
};

class UnavailableFactory final : public penumbra::PlayerFactory {
 public:
  [[nodiscard]] absl::StatusOr<penumbra::PortLimits> GetLimits()
      const override {
    return absl::UnimplementedError("no Penumbra port is linked");
  }
  absl::StatusOr<std::unique_ptr<penumbra::Player>> CreatePlayer(
      [[maybe_unused]] penumbra::PlayerConfig config,
      [[maybe_unused]] std::shared_ptr<penumbra::PlayerCallbacks> callbacks)
      override {
    return absl::UnimplementedError("no Penumbra port is linked");
  }
};

class PostedRunner final : public penumbra::ControlTaskRunner {
 public:
  explicit PostedRunner(PlayerOptions options)
      : post_(std::move(options.post_task)),
        on_sequence_(std::move(options.runs_on_sequence)) {}

  absl::Status PostTask(Task task, absl::Duration delay) override {
    auto shared = std::make_shared<Task>(std::move(task));
    post_([shared] { std::move (*shared)(); },
          absl::ToChronoMicroseconds(delay));
    return absl::OkStatus();
  }

  [[nodiscard]] bool RunsTasksInCurrentSequence() const override {
    return !on_sequence_ || on_sequence_();
  }

 private:
  std::function<void(std::function<void()>, std::chrono::microseconds)> post_;
  std::function<bool()> on_sequence_;
};

struct Listener {
  EventType type = EventType::kLoadStart;
  std::function<void(const void*)> callback;
  bool active = true;
};

struct Registry {
  std::vector<std::shared_ptr<Listener>> listeners;
};

ElementState ToElementState(const pipeline::MediaElementState& state) {
  return {.network_state = state.network,
          .ready_state = state.ready,
          .paused = state.paused,
          .seeking = state.seeking,
          .ended = state.ended,
          .current_time = ToSeconds(state.current_time),
          .duration = ToSeconds(state.duration)};
}

template <typename Event>
void Notify(const Registry& registry, const Event& event,
            const std::weak_ptr<int>& alive) {
  const std::vector<std::shared_ptr<Listener>> snapshot = registry.listeners;
  for (const std::shared_ptr<Listener>& listener : snapshot) {
    if (listener->active && listener->type == Event::kType) {
      listener->callback(&event);
      if (alive.expired()) {
        return;
      }
    }
  }
}

template <typename Event>
void NotifyPlain(const Registry& registry, const pipeline::MediaEvent& event,
                 const std::weak_ptr<int>& alive) {
  Event typed;
  typed.state = ToElementState(event.state);
  Notify(registry, typed, alive);
}

void Deliver(const pipeline::MediaEvent& event, const Registry& registry,
             const std::weak_ptr<int>& alive) {
  using pipeline::MediaEventType;
  switch (event.type) {
    case MediaEventType::kLoadStart:
      NotifyPlain<LoadStartEvent>(registry, event, alive);
      break;
    case MediaEventType::kProgress:
      NotifyPlain<ProgressEvent>(registry, event, alive);
      break;
    case MediaEventType::kSuspend:
      NotifyPlain<SuspendEvent>(registry, event, alive);
      break;
    case MediaEventType::kAbort:
      NotifyPlain<AbortEvent>(registry, event, alive);
      break;
    case MediaEventType::kEmptied:
      NotifyPlain<EmptiedEvent>(registry, event, alive);
      break;
    case MediaEventType::kLoadedMetadata:
      NotifyPlain<LoadedMetadataEvent>(registry, event, alive);
      break;
    case MediaEventType::kLoadedData:
      NotifyPlain<LoadedDataEvent>(registry, event, alive);
      break;
    case MediaEventType::kCanPlay:
      NotifyPlain<CanPlayEvent>(registry, event, alive);
      break;
    case MediaEventType::kCanPlayThrough:
      NotifyPlain<CanPlayThroughEvent>(registry, event, alive);
      break;
    case MediaEventType::kPlaying:
      NotifyPlain<PlayingEvent>(registry, event, alive);
      break;
    case MediaEventType::kWaiting:
      NotifyPlain<WaitingEvent>(registry, event, alive);
      break;
    case MediaEventType::kSeeking:
      NotifyPlain<SeekingEvent>(registry, event, alive);
      break;
    case MediaEventType::kSeeked:
      NotifyPlain<SeekedEvent>(registry, event, alive);
      break;
    case MediaEventType::kEnded:
      NotifyPlain<EndedEvent>(registry, event, alive);
      break;
    case MediaEventType::kDurationChange:
      NotifyPlain<DurationChangeEvent>(registry, event, alive);
      break;
    case MediaEventType::kTimeUpdate:
      NotifyPlain<TimeUpdateEvent>(registry, event, alive);
      break;
    case MediaEventType::kPlay:
      NotifyPlain<PlayEvent>(registry, event, alive);
      break;
    case MediaEventType::kPause:
      NotifyPlain<PauseEvent>(registry, event, alive);
      break;
    case MediaEventType::kRateChange:
      NotifyPlain<RateChangeEvent>(registry, event, alive);
      break;
    case MediaEventType::kVolumeChange:
      NotifyPlain<VolumeChangeEvent>(registry, event, alive);
      break;
    case MediaEventType::kStalled:
      NotifyPlain<StalledEvent>(registry, event, alive);
      break;
    case MediaEventType::kResize:
      NotifyPlain<ResizeEvent>(registry, event, alive);
      break;
    case MediaEventType::kError: {
      ErrorEvent typed;
      typed.state = ToElementState(event.state);
      typed.error = MediaError(
          ToPublic(event.state.error),
          event.detail.has_value() ? *event.detail : base::ErrorDetail());
      Notify(registry, typed, alive);
      break;
    }
    case MediaEventType::kWarning: {
      WarningEvent typed;
      typed.state = ToElementState(event.state);
      typed.detail =
          event.detail.has_value() ? *event.detail : base::ErrorDetail();
      Notify(registry, typed, alive);
      break;
    }
  }
}

}

class Player::Impl {
 public:
  Impl(PlayerOptions options,
       std::shared_ptr<penumbra::ControlTaskRunner> control,
       internal::PlayerDependencies dependencies)
      : factory_(std::move(dependencies.factory)),
        capabilities_(std::move(dependencies.capabilities)),
        resolve_source_(std::move(dependencies.resolve_source)),
        qoe_enabled_(std::move(options.qoe_enabled)),
        statistics_(qoe_enabled_ ? qoe_enabled_() : true),
        machine_(*factory_, std::move(control), MakeConfig(options)),
        cue_budget_kb_(options.text_cue_memory_budget_kb) {
    if (options.on_events_available) {
      machine_.Events().SetNotifyHook(std::move(options.on_events_available));
    }
  }

  const std::string& src() const { return src_; }

  void SetSrc(std::string src) {
    src_ = std::move(src);
    src_set_ = true;
    Load();
  }

  void Load() {
    if (!src_set_) {
      machine_.Load(nullptr);
      return;
    }
    std::unique_ptr<pipeline::SampleSource> source;
    if (!src_.empty() && resolve_source_) {
      source = resolve_source_(src_);
    }
    if (source == nullptr) {
      source = std::make_unique<UnsupportedSource>();
    }
    machine_.Load(std::move(source));
  }

  CanPlayTypeResult CanPlayType(std::string_view type) const {
    return internal::AnswerCanPlayType(type, capabilities_.get());
  }

  bool SeekTo(double seconds, bool fast = false) {
    if (!std::isfinite(seconds)) {
      return false;
    }
    if (fast) {
      static_cast<void>(machine_.FastSeek(FromSeconds(seconds)));
    } else {
      static_cast<void>(machine_.Seek(FromSeconds(seconds)));
    }
    UpdateTracks();
    return true;
  }

  double CurrentTime() {
    UpdateTracks();
    return ToSeconds(machine_.CurrentTime());
  }

  void UpdateTracks() {
    const double time = ToSeconds(machine_.CurrentTime());
    for (size_t index = 0; index < tracks_.length(); ++index) {
      if (const auto track = tracks_.item(index)) {
        track->Update(time);
      }
    }
  }

  TimeRanges Buffered() const {
    const auto range = machine_.GetBufferedRange();
    if (!range.has_value()) {
      return {};
    }
    return TimeRanges(
        {{.start = ToSeconds(range->start), .end = ToSeconds(range->end)}});
  }

  TimeRanges Seekable() const {
    if (machine_.GetReadyState() == ReadyState::kHaveNothing) {
      return {};
    }
    const auto range = machine_.GetSeekableRange();
    if (range.has_value()) {
      return TimeRanges(
          {{.start = ToSeconds(range->start), .end = ToSeconds(range->end)}});
    }
    if (!machine_.GetDuration().is_finite()) {
      return {};
    }
    return TimeRanges(
        {{.start = 0.0, .end = ToSeconds(machine_.GetDuration())}});
  }

  std::optional<MediaError> Error() const {
    const auto& error = machine_.GetError();
    if (!error.has_value()) {
      return std::nullopt;
    }
    return MediaError(ToPublic(error->code), error->detail);
  }

  TextTrackList& Tracks() { return tracks_; }

  std::shared_ptr<TextTrack> AddTextTrack(TextTrackKind kind, std::string label,
                                          std::string language) {
    auto track =
        std::make_shared<TextTrack>(kind, std::move(label), std::move(language),
                                    static_cast<size_t>(cue_budget_kb_) * 1024);
    tracks_.Add(track);
    return track;
  }

  void Dispatch() {
    UpdateTracks();
    const std::weak_ptr<int> alive = alive_;
    const std::shared_ptr<Registry> registry = registry_;
    for (pipeline::QueueEntry& entry : machine_.Events().Drain()) {
      if (alive.expired()) {
        return;
      }
      if (auto* completion = std::get_if<pipeline::PlayCompletion>(&entry)) {
        if (completion->callback) {
          completion->callback(std::move(completion->result));
        }
      } else {
        Deliver(std::get<pipeline::MediaEvent>(entry), *registry, alive);
      }
    }
  }

  Subscription AddListener(EventType type,
                           std::function<void(const void*)> callback) {
    auto listener = std::make_shared<Listener>();
    listener->type = type;
    listener->callback = std::move(callback);
    registry_->listeners.push_back(listener);
    return Subscription([registry = std::weak_ptr<Registry>(registry_),
                         listener = std::move(listener)] {
      listener->active = false;
      if (const std::shared_ptr<Registry> locked = registry.lock()) {
        std::erase(locked->listeners, listener);
      }
    });
  }

  pipeline::PlaybackStateMachine& Machine() { return machine_; }
  const pipeline::PlaybackStateMachine& Machine() const { return machine_; }
  bool preserves_pitch() const { return preserves_pitch_; }
  void set_preserves_pitch(bool value) { preserves_pitch_ = value; }

  StatisticsSnapshot Statistics(bool include_unstable_debug) {
    UpdateQoeEnabled();
    return statistics_.GetSnapshot(include_unstable_debug);
  }

  void ReportMetric(const observability::MetricsEvent& event) {
    UpdateQoeEnabled();
    observability::ReportMetric(statistics_, event);
  }

  static pipeline::PlaybackStateMachine::Config MakeConfig(
      const PlayerOptions& options) {
    pipeline::PlaybackStateMachine::Config config;
    config.preroll_ms = options.preroll_ms;
    config.buffer.start_threshold_ms = options.start_threshold_ms;
    config.buffer.rebuffer_threshold_ms = options.rebuffer_threshold_ms;
    config.buffer.video_max_bytes = options.video_max_bytes;
    config.buffer.audio_max_bytes = options.audio_max_bytes;
    config.allow_playback = options.allow_playback;
    config.timeupdate_interval_ms = options.timeupdate_interval_ms;
    config.fast_seek_snap = options.fast_seek_snap == FastSeekSnap::kNearest
                                ? pipeline::FastSeekSnap::kNearest
                                : pipeline::FastSeekSnap::kPrevious;
    return config;
  }

 private:
  void UpdateQoeEnabled() {
    if (qoe_enabled_) {
      statistics_.SetEnabled(qoe_enabled_());
    }
  }

  std::shared_ptr<penumbra::PlayerFactory> factory_;
  std::shared_ptr<penumbra::CapabilityProvider> capabilities_;
  internal::SourceResolver resolve_source_;
  std::function<bool()> qoe_enabled_;
  observability::StatisticsModel statistics_;
  pipeline::PlaybackStateMachine machine_;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  std::string src_;
  bool src_set_ = false;
  std::shared_ptr<Registry> registry_ = std::make_shared<Registry>();
  bool preserves_pitch_ = true;
  TextTrackList tracks_;
  int cue_budget_kb_;
};

namespace internal {

std::unique_ptr<Player> PlayerAccess::Create(
    PlayerOptions options, std::shared_ptr<penumbra::ControlTaskRunner> control,
    PlayerDependencies dependencies) {
  if (control == nullptr || dependencies.factory == nullptr ||
      options.text_cue_memory_budget_kb < 64 ||
      options.text_cue_memory_budget_kb > 65536 ||
      !pipeline::PlaybackStateMachine::ValidateConfig(
           Player::Impl::MakeConfig(options))
           .ok()) {
    return nullptr;
  }
  return std::unique_ptr<Player>(new Player(std::make_unique<Player::Impl>(
      std::move(options), std::move(control), std::move(dependencies))));
}

void PlayerAccess::ReportMetric(Player& player,
                                const observability::MetricsEvent& event) {
  player.impl_->ReportMetric(event);
}

}

std::unique_ptr<Player> Player::Create(PlayerOptions options) {
  if (!options.post_task) {
    return nullptr;
  }
  auto control = std::make_shared<PostedRunner>(options);
  internal::PlayerDependencies dependencies;
  dependencies.factory = std::make_shared<UnavailableFactory>();
  return internal::PlayerAccess::Create(std::move(options), std::move(control),
                                        std::move(dependencies));
}

Player::Player(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Player::~Player() = default;

const std::string& Player::src() const { return impl_->src(); }

void Player::set_src(std::string src) { impl_->SetSrc(std::move(src)); }

void Player::Load() { impl_->Load(); }

CanPlayTypeResult Player::CanPlayType(std::string_view type) const {
  return impl_->CanPlayType(type);
}

NetworkState Player::network_state() const {
  return impl_->Machine().GetNetworkState();
}

void Player::Play(PlayCallback done) { impl_->Machine().Play(std::move(done)); }

void Player::Pause() { impl_->Machine().Pause(); }

bool Player::paused() const { return impl_->Machine().IsPaused(); }

bool Player::ended() const { return impl_->Machine().IsEnded(); }

bool Player::autoplay() const { return impl_->Machine().GetAutoplay(); }

void Player::set_autoplay(bool autoplay) {
  impl_->Machine().SetAutoplay(autoplay);
}

bool Player::loop() const { return impl_->Machine().GetLoop(); }

void Player::set_loop(bool loop) { impl_->Machine().SetLoop(loop); }

double Player::current_time() const { return impl_->CurrentTime(); }

bool Player::set_current_time(double seconds) { return impl_->SeekTo(seconds); }

bool Player::FastSeek(double seconds) { return impl_->SeekTo(seconds, true); }

bool Player::seeking() const { return impl_->Machine().IsSeeking(); }

double Player::duration() const {
  return ToSeconds(impl_->Machine().GetDuration());
}

TimeRanges Player::buffered() const { return impl_->Buffered(); }

TimeRanges Player::seekable() const { return impl_->Seekable(); }

ReadyState Player::ready_state() const {
  return impl_->Machine().GetReadyState();
}

Preload Player::preload() const { return impl_->Machine().GetPreload(); }

void Player::set_preload(Preload preload) {
  impl_->Machine().SetPreload(preload);
}

double Player::playback_rate() const {
  return impl_->Machine().GetPlaybackRate();
}

bool Player::set_playback_rate(double rate) {
  return impl_->Machine().SetPlaybackRate(rate).ok();
}

double Player::default_playback_rate() const {
  return impl_->Machine().GetDefaultPlaybackRate();
}

bool Player::set_default_playback_rate(double rate) {
  return impl_->Machine().SetDefaultPlaybackRate(rate).ok();
}

bool Player::preserves_pitch() const { return impl_->preserves_pitch(); }

void Player::set_preserves_pitch(bool preserves_pitch) {
  impl_->set_preserves_pitch(preserves_pitch);
}

double Player::volume() const { return impl_->Machine().GetVolume(); }

bool Player::set_volume(double volume) {
  return impl_->Machine().SetVolume(volume).ok();
}

bool Player::muted() const { return impl_->Machine().IsMuted(); }

void Player::set_muted(bool muted) {
  static_cast<void>(impl_->Machine().SetMuted(muted));
}

std::optional<MediaError> Player::error() const { return impl_->Error(); }

StatisticsSnapshot Player::Statistics(bool include_unstable_debug) const {
  return impl_->Statistics(include_unstable_debug);
}

TextTrackList& Player::text_tracks() { return impl_->Tracks(); }

std::shared_ptr<TextTrack> Player::AddTextTrack(TextTrackKind kind,
                                                std::string label,
                                                std::string language) {
  return impl_->AddTextTrack(kind, std::move(label), std::move(language));
}

void Player::DispatchEvents() { impl_->Dispatch(); }

Subscription Player::AddListener(EventType type,
                                 std::function<void(const void*)> listener) {
  return impl_->AddListener(type, std::move(listener));
}

}
