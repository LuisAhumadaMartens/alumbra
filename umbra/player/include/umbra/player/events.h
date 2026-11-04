#ifndef UMBRA_PLAYER_EVENTS_H_
#define UMBRA_PLAYER_EVENTS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "umbra/base/error_detail.h"
#include "umbra/player/media_error.h"
#include "umbra/player/media_state.h"

namespace umbra::player {

enum class EventType : uint8_t {
  kLoadStart,
  kProgress,
  kSuspend,
  kAbort,
  kError,
  kEmptied,
  kStalled,
  kLoadedMetadata,
  kLoadedData,
  kCanPlay,
  kCanPlayThrough,
  kPlaying,
  kWaiting,
  kSeeking,
  kSeeked,
  kEnded,
  kDurationChange,
  kTimeUpdate,
  kPlay,
  kPause,
  kRateChange,
  kResize,
  kVolumeChange,
  kWarning,
  kAudioTrackAdd,
  kAudioTrackRemove,
  kAudioTracksChange,
  kVideoTrackAdd,
  kVideoTrackRemove,
  kVideoTracksChange,
  kTextTrackAdd,
  kTextTrackRemove,
  kTextTracksChange,
};

struct ElementState {
  NetworkState network_state = NetworkState::kEmpty;
  ReadyState ready_state = ReadyState::kHaveNothing;
  bool paused = true;
  bool seeking = false;
  bool ended = false;
  double current_time = 0.0;
  double duration = 0.0;
};

struct LoadStartEvent {
  static constexpr EventType kType = EventType::kLoadStart;
  static constexpr std::string_view kName = "loadstart";
  ElementState state;
};

struct ProgressEvent {
  static constexpr EventType kType = EventType::kProgress;
  static constexpr std::string_view kName = "progress";
  ElementState state;
};

struct SuspendEvent {
  static constexpr EventType kType = EventType::kSuspend;
  static constexpr std::string_view kName = "suspend";
  ElementState state;
};

struct AbortEvent {
  static constexpr EventType kType = EventType::kAbort;
  static constexpr std::string_view kName = "abort";
  ElementState state;
};

struct ErrorEvent {
  static constexpr EventType kType = EventType::kError;
  static constexpr std::string_view kName = "error";
  ElementState state;
  MediaError error{MediaErrorCode::kDecode, base::ErrorDetail()};
};

struct EmptiedEvent {
  static constexpr EventType kType = EventType::kEmptied;
  static constexpr std::string_view kName = "emptied";
  ElementState state;
};

struct StalledEvent {
  static constexpr EventType kType = EventType::kStalled;
  static constexpr std::string_view kName = "stalled";
  ElementState state;
};

struct LoadedMetadataEvent {
  static constexpr EventType kType = EventType::kLoadedMetadata;
  static constexpr std::string_view kName = "loadedmetadata";
  ElementState state;
};

struct LoadedDataEvent {
  static constexpr EventType kType = EventType::kLoadedData;
  static constexpr std::string_view kName = "loadeddata";
  ElementState state;
};

struct CanPlayEvent {
  static constexpr EventType kType = EventType::kCanPlay;
  static constexpr std::string_view kName = "canplay";
  ElementState state;
};

struct CanPlayThroughEvent {
  static constexpr EventType kType = EventType::kCanPlayThrough;
  static constexpr std::string_view kName = "canplaythrough";
  ElementState state;
};

struct PlayingEvent {
  static constexpr EventType kType = EventType::kPlaying;
  static constexpr std::string_view kName = "playing";
  ElementState state;
};

struct WaitingEvent {
  static constexpr EventType kType = EventType::kWaiting;
  static constexpr std::string_view kName = "waiting";
  ElementState state;
};

struct SeekingEvent {
  static constexpr EventType kType = EventType::kSeeking;
  static constexpr std::string_view kName = "seeking";
  ElementState state;
};

struct SeekedEvent {
  static constexpr EventType kType = EventType::kSeeked;
  static constexpr std::string_view kName = "seeked";
  ElementState state;
};

struct EndedEvent {
  static constexpr EventType kType = EventType::kEnded;
  static constexpr std::string_view kName = "ended";
  ElementState state;
};

struct DurationChangeEvent {
  static constexpr EventType kType = EventType::kDurationChange;
  static constexpr std::string_view kName = "durationchange";
  ElementState state;
};

struct TimeUpdateEvent {
  static constexpr EventType kType = EventType::kTimeUpdate;
  static constexpr std::string_view kName = "timeupdate";
  ElementState state;
};

struct PlayEvent {
  static constexpr EventType kType = EventType::kPlay;
  static constexpr std::string_view kName = "play";
  ElementState state;
};

struct PauseEvent {
  static constexpr EventType kType = EventType::kPause;
  static constexpr std::string_view kName = "pause";
  ElementState state;
};

struct RateChangeEvent {
  static constexpr EventType kType = EventType::kRateChange;
  static constexpr std::string_view kName = "ratechange";
  ElementState state;
};

struct ResizeEvent {
  static constexpr EventType kType = EventType::kResize;
  static constexpr std::string_view kName = "resize";
  ElementState state;
};

struct VolumeChangeEvent {
  static constexpr EventType kType = EventType::kVolumeChange;
  static constexpr std::string_view kName = "volumechange";
  ElementState state;
};

struct WarningEvent {
  static constexpr EventType kType = EventType::kWarning;
  static constexpr std::string_view kName = "warning";
  ElementState state;
  base::ErrorDetail detail;
};

struct AudioTrackAddEvent {
  static constexpr EventType kType = EventType::kAudioTrackAdd;
  static constexpr std::string_view kName = "addtrack";
  ElementState state;
  std::string track_id;
};

struct AudioTrackRemoveEvent {
  static constexpr EventType kType = EventType::kAudioTrackRemove;
  static constexpr std::string_view kName = "removetrack";
  ElementState state;
  std::string track_id;
};

struct AudioTracksChangeEvent {
  static constexpr EventType kType = EventType::kAudioTracksChange;
  static constexpr std::string_view kName = "change";
  ElementState state;
};

struct VideoTrackAddEvent {
  static constexpr EventType kType = EventType::kVideoTrackAdd;
  static constexpr std::string_view kName = "addtrack";
  ElementState state;
  std::string track_id;
};

struct VideoTrackRemoveEvent {
  static constexpr EventType kType = EventType::kVideoTrackRemove;
  static constexpr std::string_view kName = "removetrack";
  ElementState state;
  std::string track_id;
};

struct VideoTracksChangeEvent {
  static constexpr EventType kType = EventType::kVideoTracksChange;
  static constexpr std::string_view kName = "change";
  ElementState state;
};

struct TextTrackAddEvent {
  static constexpr EventType kType = EventType::kTextTrackAdd;
  static constexpr std::string_view kName = "addtrack";
  ElementState state;
};

struct TextTrackRemoveEvent {
  static constexpr EventType kType = EventType::kTextTrackRemove;
  static constexpr std::string_view kName = "removetrack";
  ElementState state;
};

struct TextTracksChangeEvent {
  static constexpr EventType kType = EventType::kTextTracksChange;
  static constexpr std::string_view kName = "change";
  ElementState state;
};

class Subscription {
 public:
  Subscription() = default;
  explicit Subscription(std::function<void()> cancel)
      : cancel_(std::move(cancel)) {}
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  Subscription(Subscription&& other) noexcept
      : cancel_(std::move(other.cancel_)) {
    other.cancel_ = nullptr;
  }
  Subscription& operator=(Subscription&& other) noexcept {
    if (this != &other) {
      Cancel();
      cancel_ = std::move(other.cancel_);
      other.cancel_ = nullptr;
    }
    return *this;
  }
  ~Subscription() noexcept { Cancel(); }

  void Cancel() {
    if (cancel_) {
      const std::function<void()> cancel = std::move(cancel_);
      cancel_ = nullptr;
      cancel();
    }
  }
  [[nodiscard]] bool active() const { return static_cast<bool>(cancel_); }

 private:
  std::function<void()> cancel_;
};

}

#endif
