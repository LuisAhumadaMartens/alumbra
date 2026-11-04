#include "umbra/player/player.h"

#include <algorithm>
#include <array>
#include <atomic>
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
#include "absl/time/time.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/observability/log.h"
#include "umbra/observability/metrics.h"
#include "umbra/observability/statistics_model.h"
#include "umbra/observability/trace.h"
#include "umbra/pipeline/event_queue.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/playback_state_machine.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/can_play_type.h"
#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"
#include "umbra/player/observability.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player_access.h"
#include "umbra/player/port_access.h"
#include "umbra/player/settings.h"
#include "umbra/player/statistics.h"
#include "umbra/player/text_track.h"
#include "umbra/player/text_track_routing.h"
#include "umbra/player/time_ranges.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_value.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::player {
namespace {

constexpr double kMicrosecondsPerSecond = 1e6;
constexpr double kMaxMicroseconds = 9.0e18;

settings::Platform CurrentPlatform() {
#ifdef __APPLE__
  return settings::Platform::kMacos;
#elif _WIN32
  return settings::Platform::kWindows;
#else
  return settings::Platform::kLinux;
#endif
}

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

struct Listener {
  EventType type = EventType::kLoadStart;
  std::function<void(const std::any&)> callback;
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
      listener->callback(std::any(event));
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

class Settings::Impl {
 public:
  explicit Impl(settings::Platform platform)
      : store_(settings::GetRegistry(), platform) {}

  settings::Store& store() { return store_; }
  const settings::Store& store() const { return store_; }

  std::function<void(std::string_view, const SettingValue&)> change_handler_;

 private:
  settings::Store store_;
};

std::optional<TraceCategory> ToTraceCategory(std::string_view name) {
  const std::array<std::pair<std::string_view, TraceCategory>, 14> categories{{
      {"net", TraceCategory::kNet},
      {"manifest", TraceCategory::kManifest},
      {"abr", TraceCategory::kAbr},
      {"buffer", TraceCategory::kBuffer},
      {"demux", TraceCategory::kDemux},
      {"decode", TraceCategory::kDecode},
      {"render", TraceCategory::kRender},
      {"audio", TraceCategory::kAudio},
      {"text", TraceCategory::kText},
      {"ads", TraceCategory::kAds},
      {"drm", TraceCategory::kDrm},
      {"settings", TraceCategory::kSettings},
      {"player", TraceCategory::kPlayer},
      {"port", TraceCategory::kPort},
  }};
  const auto found = std::ranges::find_if(
      categories, [name](const auto& item) { return item.first == name; });
  if (found == categories.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::uint32_t ReadCategoryMask(const Settings& settings, std::string_view key,
                               std::uint32_t empty_mask) {
  const auto value = settings.Get(key);
  if (!value.has_value() ||
      !std::holds_alternative<std::vector<std::string>>(*value)) {
    return empty_mask;
  }
  const std::vector<std::string>& names =
      std::get<std::vector<std::string>>(*value);
  if (names.empty()) {
    return empty_mask;
  }
  std::uint32_t mask = 0;
  for (const std::string& name : names) {
    if (const auto category = ToTraceCategory(name); category.has_value()) {
      mask |= std::uint32_t{1} << static_cast<unsigned>(*category);
    }
  }
  return mask;
}

LogLevel ParseLogLevel(std::string_view name) {
  if (name == "verbose") {
    return LogLevel::kVerbose;
  }
  if (name == "debug") {
    return LogLevel::kDebug;
  }
  if (name == "warning") {
    return LogLevel::kWarning;
  }
  if (name == "error") {
    return LogLevel::kError;
  }
  return LogLevel::kInfo;
}

LogLevel ReadLogLevel(const Settings& settings) {
  const auto value = settings.Get("observability.log.level");
  if (!value.has_value() || !std::holds_alternative<std::string>(*value)) {
    return LogLevel::kInfo;
  }
  return ParseLogLevel(std::get<std::string>(*value));
}

Settings::Settings(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Settings::~Settings() = default;

std::optional<base::ErrorDetail> Settings::Set(std::string_view key,
                                               SettingValue value) {
  const absl::Status status =
      impl_->store().SetTyped(settings::Layer::kIntegrator, key, value);
  if (!status.ok()) {
    return base::ErrorDetail(
        base::ErrorCode::kSettingsInvalid, base::ErrorCategory::kPlayer,
        base::ErrorSeverity::kFatal, false,
        {{"key", std::string(key)}, {"reason", std::string(status.message())}},
        nullptr);
  }
  if (impl_->change_handler_) {
    impl_->change_handler_(key, value);
  }
  return std::nullopt;
}

std::optional<SettingValue> Settings::Get(std::string_view key) const {
  const settings::Registry& schema = impl_->store().Schema();
  const auto found = settings::FindSetting(schema, key);
  if (!found.has_value()) {
    return std::nullopt;
  }
  return impl_->store().TakeSnapshot().GetValue(found->index);
}

void Settings::SetChangeHandler(
    std::function<void(std::string_view, const SettingValue&)> handler) {
  impl_->change_handler_ = std::move(handler);
}

class Player::Impl {
 public:
  Impl(PlayerOptions options,
       std::shared_ptr<penumbra::ControlTaskRunner> control,
       internal::PlayerDependencies dependencies)
      : factory_(std::move(dependencies.factory)),
        capabilities_(std::move(dependencies.capabilities)),
        port_(std::move(dependencies.port)),
        resolve_source_(std::move(dependencies.resolve_source)),
        load_text_track_(std::move(dependencies.load_text_track)),
        qoe_enabled_(std::move(options.qoe_enabled)),
        statistics_(qoe_enabled_ ? qoe_enabled_() : true),
        settings_(std::unique_ptr<Settings::Impl>(
            new Settings::Impl(CurrentPlatform()))),
        control_(control),
        machine_(*factory_, std::move(control), MakeConfig(options, settings_)),
        log_level_(ReadLogLevel(settings_)),
        log_category_mask_(ReadCategoryMask(settings_,
                                            "observability.log.categories",
                                            (std::uint32_t{1} << 14) - 1)),
        trace_category_mask_(
            ReadCategoryMask(settings_, "observability.trace.categories", 0)),
        log_sink_(std::move(options.log_sink)),
        trace_sink_(std::move(options.trace_sink)),
        cue_budget_kb_(options.text_cue_memory_budget_kb) {
    if (options.on_events_available) {
      machine_.Events().SetNotifyHook(std::move(options.on_events_available));
    }
    settings_.SetChangeHandler(
        [this, alive = std::weak_ptr<int>(alive_)](std::string_view key,
                                                   const SettingValue& value) {
          const std::string owned_key(key);
          const SettingValue owned_value(value);
          if (control_ == nullptr ||
              !control_
                   ->PostTask(
                       [this, alive, owned_key, owned_value] {
                         if (!alive.expired()) {
                           ApplySetting(owned_key, owned_value);
                         }
                       },
                       absl::ZeroDuration())
                   .ok()) {
            return;
          }
        });
  }

  const std::string& src() const { return src_; }

  void SetSrc(std::string src) {
    src_ = std::move(src);
    src_set_ = true;
    Load();
  }

  void Load() {
    Log(TraceCategory::kPlayer, LogLevel::kInfo, "load requested");
    Trace(TraceCategory::kPlayer, "player.load");
    if (!src_set_) {
      machine_.Load(nullptr);
      return;
    }
    std::unique_ptr<pipeline::SampleSource> source;
    if (!src_.empty() && resolve_source_) {
      source = resolve_source_(src_, &tracks_, TextTrackOptions());
    }
    if (source == nullptr) {
      source = std::make_unique<UnsupportedSource>();
    }
    machine_.Load(std::move(source));
  }

  CanPlayTypeResult CanPlayType(std::string_view type) const {
    return internal::AnswerCanPlayType(type, capabilities_.get());
  }

  internal::TextTrackRoutingOptions TextTrackOptions() const {
    internal::TextTrackRoutingOptions options;
    options.cue_memory_budget_bytes =
        static_cast<size_t>(cue_budget_kb_) * 1024;
    const auto max_bytes = settings_.Get("text.sideload.max_bytes");
    const auto fallback_encoding = settings_.Get("text.srt.fallback_encoding");
    const auto auto_detect = settings_.Get("text.charset.auto_detect");
    const auto auto_discover = settings_.Get("text.sideload.auto_discover");
    if (max_bytes.has_value()) {
      options.max_payload_bytes =
          static_cast<size_t>(std::get<std::int64_t>(*max_bytes));
    }
    if (fallback_encoding.has_value()) {
      options.fallback_encoding = std::get<std::string>(*fallback_encoding);
    }
    if (auto_detect.has_value()) {
      options.auto_detect_encoding = std::get<bool>(*auto_detect);
    }
    if (auto_discover.has_value()) {
      options.auto_discover_siblings = std::get<bool>(*auto_discover);
    }
    return options;
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

  std::shared_ptr<TextTrack> AddTextTrackFromUrl(std::string url,
                                                 std::string label,
                                                 std::string language,
                                                 TextTrackKind kind) {
    std::shared_ptr<TextTrack> track =
        AddTextTrack(kind, std::move(label), std::move(language));
    if (!load_text_track_) {
      tracks_.Remove(track->id());
      return nullptr;
    }
    const std::optional<SettingValue> max_bytes_setting =
        settings_.Get("text.sideload.max_bytes");
    const std::optional<SettingValue> fallback_setting =
        settings_.Get("text.srt.fallback_encoding");
    const std::optional<SettingValue> auto_detect_setting =
        settings_.Get("text.charset.auto_detect");
    if (!max_bytes_setting.has_value() || !fallback_setting.has_value() ||
        !auto_detect_setting.has_value()) {
      tracks_.Remove(track->id());
      return nullptr;
    }
    const size_t max_bytes =
        static_cast<size_t>(std::get<std::int64_t>(*max_bytes_setting));
    const std::string fallback_encoding =
        std::get<std::string>(*fallback_setting);
    const bool auto_detect = std::get<bool>(*auto_detect_setting);
    const std::weak_ptr<int> alive = alive_;
    load_text_track_(
        std::move(url), max_bytes,
        [this, alive, track, fallback_encoding,
         auto_detect](absl::StatusOr<std::string> result) mutable {
          if (alive.expired()) {
            return;
          }
          static_cast<void>(control_->PostTask(
              [this, alive, track, fallback_encoding, auto_detect,
               result = std::move(result)]() mutable {
                if (alive.expired()) {
                  return;
                }
                if (!result.ok() ||
                    !internal::LoadTextTrackPayload(
                        *track, *result, std::move(fallback_encoding),
                        auto_detect)) {
                  tracks_.Remove(track->id());
                }
              },
              absl::ZeroDuration()));
        });
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
                           std::function<void(const std::any&)> callback) {
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
  bool preserves_pitch() const { return machine_.PreservesPitch(); }
  void set_preserves_pitch(bool value) {
    static_cast<void>(machine_.SetPreservesPitch(value));
  }

  StatisticsSnapshot Statistics(bool include_unstable_debug) {
    UpdateQoeEnabled();
    return statistics_.GetSnapshot(include_unstable_debug);
  }

  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  std::shared_ptr<penumbra::Port> port() const { return port_; }

  void Log(TraceCategory category, LogLevel level, std::string message) const {
    if (!log_sink_ ||
        static_cast<unsigned>(level) <
            static_cast<unsigned>(log_level_.load(std::memory_order_relaxed)) ||
        (log_category_mask_.load(std::memory_order_relaxed) &
         (std::uint32_t{1} << static_cast<unsigned>(category))) == 0) {
      return;
    }
    log_sink_({.category = category,
               .level = level,
               .message = std::move(message),
               .fields = {}});
  }

  void Trace(TraceCategory category, std::string name) const {
    if (trace_sink_ &&
        (trace_category_mask_.load(std::memory_order_relaxed) &
         (std::uint32_t{1} << static_cast<unsigned>(category))) != 0) {
      trace_sink_({.category = category,
                   .phase = TracePhase::kInstant,
                   .name = std::move(name),
                   .value = 0});
    }
  }

  void ReportMetric(const observability::MetricsEvent& event) {
    UpdateQoeEnabled();
    observability::ReportMetric(statistics_, event);
  }

  static pipeline::PlaybackStateMachine::Config MakeConfig(
      const PlayerOptions& options, const Settings& settings) {
    pipeline::PlaybackStateMachine::Config config;
    config.preroll_ms = options.preroll_ms;
    config.buffer.start_threshold_ms =
        std::get<std::int64_t>(*settings.Get("buffer.start_threshold_ms"));
    config.buffer.rebuffer_threshold_ms =
        std::get<std::int64_t>(*settings.Get("buffer.rebuffer_threshold_ms"));
    config.buffer.video_max_bytes = static_cast<std::uint64_t>(
        std::get<std::int64_t>(*settings.Get("buffer.video_max_bytes")));
    config.buffer.audio_max_bytes = static_cast<std::uint64_t>(
        std::get<std::int64_t>(*settings.Get("buffer.audio_max_bytes")));
    config.allow_playback = options.allow_playback;
    config.timeupdate_interval_ms = static_cast<int>(std::get<std::int64_t>(
        *settings.Get("player.events.timeupdate_interval_ms")));
    config.minimum_audible_rate = options.minimum_audible_rate;
    config.maximum_audible_rate = options.maximum_audible_rate;
    config.fast_seek_snap = options.fast_seek_snap == FastSeekSnap::kNearest
                                ? pipeline::FastSeekSnap::kNearest
                                : pipeline::FastSeekSnap::kPrevious;
    return config;
  }

  void ApplySetting(std::string_view key, const SettingValue& value) {
    if (key == "observability.log.level" &&
        std::holds_alternative<std::string>(value)) {
      log_level_.store(ParseLogLevel(std::get<std::string>(value)),
                       std::memory_order_relaxed);
      return;
    }
    if ((key == "observability.log.categories" ||
         key == "observability.trace.categories") &&
        std::holds_alternative<std::vector<std::string>>(value)) {
      std::uint32_t mask = 0;
      for (const std::string& name :
           std::get<std::vector<std::string>>(value)) {
        if (const auto category = ToTraceCategory(name); category.has_value()) {
          mask |= std::uint32_t{1} << static_cast<unsigned>(*category);
        }
      }
      if (key == "observability.log.categories") {
        log_category_mask_.store(
            mask == 0 && std::get<std::vector<std::string>>(value).empty()
                ? (std::uint32_t{1} << 14) - 1
                : mask,
            std::memory_order_relaxed);
      } else {
        trace_category_mask_.store(mask, std::memory_order_relaxed);
      }
      return;
    }
    if (key == "player.events.timeupdate_interval_ms") {
      static_cast<void>(machine_.SetTimeUpdateInterval(
          static_cast<int>(std::get<std::int64_t>(value))));
      return;
    }
    if (key == "buffer.start_threshold_ms" ||
        key == "buffer.rebuffer_threshold_ms" ||
        key == "buffer.video_max_bytes" || key == "buffer.audio_max_bytes") {
      pipeline::BufferManager::Config config = machine_.GetBufferConfig();
      if (key == "buffer.start_threshold_ms") {
        config.start_threshold_ms = std::get<std::int64_t>(value);
      } else if (key == "buffer.rebuffer_threshold_ms") {
        config.rebuffer_threshold_ms = std::get<std::int64_t>(value);
      } else if (key == "buffer.video_max_bytes") {
        config.video_max_bytes =
            static_cast<std::uint64_t>(std::get<std::int64_t>(value));
      } else {
        config.audio_max_bytes =
            static_cast<std::uint64_t>(std::get<std::int64_t>(value));
      }
      static_cast<void>(machine_.SetBufferConfig(config));
    }
  }

 private:
  void UpdateQoeEnabled() {
    if (qoe_enabled_) {
      statistics_.SetEnabled(qoe_enabled_());
    }
  }

  std::shared_ptr<penumbra::PlayerFactory> factory_;
  std::shared_ptr<penumbra::CapabilityProvider> capabilities_;
  std::shared_ptr<penumbra::Port> port_;
  internal::SourceResolver resolve_source_;
  internal::TextTrackUrlLoader load_text_track_;
  std::function<bool()> qoe_enabled_;
  observability::StatisticsModel statistics_;
  Settings settings_;
  std::shared_ptr<penumbra::ControlTaskRunner> control_;
  pipeline::PlaybackStateMachine machine_;
  std::atomic<LogLevel> log_level_;
  std::atomic<std::uint32_t> log_category_mask_;
  std::atomic<std::uint32_t> trace_category_mask_;
  LogSink log_sink_;
  TraceSink trace_sink_;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  std::string src_;
  bool src_set_ = false;
  std::shared_ptr<Registry> registry_ = std::make_shared<Registry>();
  TextTrackList tracks_;
  int cue_budget_kb_;
};

namespace internal {

std::unique_ptr<Player> PlayerAccess::Create(
    PlayerOptions options, std::shared_ptr<penumbra::ControlTaskRunner> control,
    PlayerDependencies dependencies) {
  Settings settings(
      std::unique_ptr<Settings::Impl>(new Settings::Impl(CurrentPlatform())));
  if (control == nullptr || dependencies.factory == nullptr ||
      options.text_cue_memory_budget_kb < 64 ||
      options.text_cue_memory_budget_kb > 65536 ||
      !pipeline::PlaybackStateMachine::ValidateConfig(
           Player::Impl::MakeConfig(options, settings))
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

std::shared_ptr<penumbra::Port> PlayerAccess::GetPort(Player& player) {
  return player.impl_->port();
}

std::shared_ptr<penumbra::Port> GetPort(Player& player) {
  return PlayerAccess::GetPort(player);
}

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

void Player::Play(PlayCallback done) {
  impl_->Log(TraceCategory::kPlayer, LogLevel::kInfo, "play requested");
  impl_->Trace(TraceCategory::kPlayer, "player.play");
  impl_->Machine().Play(std::move(done));
}

void Player::Pause() {
  impl_->Log(TraceCategory::kPlayer, LogLevel::kInfo, "pause requested");
  impl_->Trace(TraceCategory::kPlayer, "player.pause");
  impl_->Machine().Pause();
}

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

Settings& Player::settings() { return impl_->settings(); }

const Settings& Player::settings() const { return impl_->settings(); }

TextTrackList& Player::text_tracks() { return impl_->Tracks(); }

std::shared_ptr<TextTrack> Player::AddTextTrack(TextTrackKind kind,
                                                std::string label,
                                                std::string language) {
  return impl_->AddTextTrack(kind, std::move(label), std::move(language));
}

std::shared_ptr<TextTrack> Player::AddTextTrackFromUrl(std::string url,
                                                       std::string label,
                                                       std::string language,
                                                       TextTrackKind kind) {
  return impl_->AddTextTrackFromUrl(std::move(url), std::move(label),
                                    std::move(language), kind);
}

void Player::DispatchEvents() { impl_->Dispatch(); }

Subscription Player::AddListener(
    EventType type, std::function<void(const std::any&)> listener) {
  return impl_->AddListener(type, std::move(listener));
}

}
