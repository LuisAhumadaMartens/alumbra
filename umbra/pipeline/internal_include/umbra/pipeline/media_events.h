#ifndef UMBRA_PIPELINE_MEDIA_EVENTS_H_
#define UMBRA_PIPELINE_MEDIA_EVENTS_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <variant>

#include "penumbra/interface/media_time.h"
#include "umbra/base/error_detail.h"

namespace umbra::pipeline {

enum class NetworkState : uint8_t {
  kEmpty = 0,
  kIdle = 1,
  kLoading = 2,
  kNoSource = 3,
};

enum class ReadyState : uint8_t {
  kHaveNothing = 0,
  kHaveMetadata = 1,
  kHaveCurrentData = 2,
  kHaveFutureData = 3,
  kHaveEnoughData = 4,
};

enum class MediaEventType : uint8_t {
  kLoadStart,
  kProgress,
  kSuspend,
  kAbort,
  kError,
  kEmptied,
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
  kVolumeChange,
};

enum class MediaErrorCode : int {
  kNone = 0,
  kAborted = 1,
  kNetwork = 2,
  kDecode = 3,
  kSrcNotSupported = 4,
};

struct MediaElementState {
  NetworkState network = NetworkState::kEmpty;
  ReadyState ready = ReadyState::kHaveNothing;
  bool paused = true;
  bool seeking = false;
  bool ended = false;
  penumbra::MediaTime current_time;
  penumbra::MediaTime duration = penumbra::MediaTime::Unknown();
  MediaErrorCode error = MediaErrorCode::kNone;

  friend bool operator==(const MediaElementState&,
                         const MediaElementState&) = default;
};

struct MediaEvent {
  MediaEventType type = MediaEventType::kLoadStart;
  MediaElementState state;
};

enum class PlayResultCode : uint8_t {
  kOk,
  kNotAllowed,
  kNotSupported,
  kAbort,
};

struct PlayResult {
  PlayResultCode code = PlayResultCode::kOk;
  std::optional<base::ErrorDetail> detail;
};

using PlayCallback = std::function<void(PlayResult)>;

struct PlayCompletion {
  PlayCallback callback;
  PlayResult result;
};

using QueueEntry = std::variant<MediaEvent, PlayCompletion>;

std::string_view MediaEventName(MediaEventType type);
std::string_view NetworkStateName(NetworkState state);
std::string_view ReadyStateName(ReadyState state);

}

#endif
