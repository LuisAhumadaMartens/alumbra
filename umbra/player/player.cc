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
#include "umbra/pipeline/event_queue.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/playback_state_machine.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/can_play_type.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player_access.h"
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

}

class Player::Impl {
 public:
  Impl(PlayerOptions options,
       std::shared_ptr<penumbra::ControlTaskRunner> control,
       internal::PlayerDependencies dependencies)
      : factory_(std::move(dependencies.factory)),
        capabilities_(std::move(dependencies.capabilities)),
        resolve_source_(std::move(dependencies.resolve_source)),
        machine_(*factory_, std::move(control), MakeConfig(options)) {
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

  bool SeekTo(double seconds) {
    if (!std::isfinite(seconds)) {
      return false;
    }
    static_cast<void>(machine_.Seek(FromSeconds(seconds)));
    return true;
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
    if (machine_.GetReadyState() == ReadyState::kHaveNothing ||
        !machine_.GetDuration().is_finite()) {
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

  void Dispatch() {
    const std::weak_ptr<int> alive = alive_;
    for (pipeline::QueueEntry& entry : machine_.Events().Drain()) {
      if (alive.expired()) {
        return;
      }
      if (auto* completion = std::get_if<pipeline::PlayCompletion>(&entry)) {
        if (completion->callback) {
          completion->callback(std::move(completion->result));
        }
      }
    }
  }

  pipeline::PlaybackStateMachine& Machine() { return machine_; }
  const pipeline::PlaybackStateMachine& Machine() const { return machine_; }
  Preload preload() const { return preload_; }
  void set_preload(Preload preload) { preload_ = preload; }
  bool preserves_pitch() const { return preserves_pitch_; }
  void set_preserves_pitch(bool value) { preserves_pitch_ = value; }

  static pipeline::PlaybackStateMachine::Config MakeConfig(
      const PlayerOptions& options) {
    pipeline::PlaybackStateMachine::Config config;
    config.preroll_ms = options.preroll_ms;
    config.allow_playback = options.allow_playback;
    return config;
  }

 private:
  std::shared_ptr<penumbra::PlayerFactory> factory_;
  std::shared_ptr<penumbra::CapabilityProvider> capabilities_;
  internal::SourceResolver resolve_source_;
  pipeline::PlaybackStateMachine machine_;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  std::string src_;
  bool src_set_ = false;
  Preload preload_ = Preload::kMetadata;
  bool preserves_pitch_ = true;
};

namespace internal {

std::unique_ptr<Player> PlayerAccess::Create(
    PlayerOptions options, std::shared_ptr<penumbra::ControlTaskRunner> control,
    PlayerDependencies dependencies) {
  if (control == nullptr || dependencies.factory == nullptr ||
      !pipeline::PlaybackStateMachine::ValidateConfig(
           Player::Impl::MakeConfig(options))
           .ok()) {
    return nullptr;
  }
  return std::unique_ptr<Player>(new Player(std::make_unique<Player::Impl>(
      std::move(options), std::move(control), std::move(dependencies))));
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

double Player::current_time() const {
  return ToSeconds(impl_->Machine().CurrentTime());
}

bool Player::set_current_time(double seconds) { return impl_->SeekTo(seconds); }

bool Player::FastSeek(double seconds) { return impl_->SeekTo(seconds); }

bool Player::seeking() const { return impl_->Machine().IsSeeking(); }

double Player::duration() const {
  return ToSeconds(impl_->Machine().GetDuration());
}

TimeRanges Player::buffered() const { return impl_->Buffered(); }

TimeRanges Player::seekable() const { return impl_->Seekable(); }

ReadyState Player::ready_state() const {
  return impl_->Machine().GetReadyState();
}

Preload Player::preload() const { return impl_->preload(); }

void Player::set_preload(Preload preload) { impl_->set_preload(preload); }

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

void Player::DispatchEvents() { impl_->Dispatch(); }

}
