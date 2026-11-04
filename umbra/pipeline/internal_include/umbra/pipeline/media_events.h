#ifndef UMBRA_PIPELINE_MEDIA_EVENTS_H_
#define UMBRA_PIPELINE_MEDIA_EVENTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "penumbra/interface/media_time.h"
#include "umbra/base/error_detail.h"
#include "umbra/player/media_state.h"
#include "umbra/player/play_result.h"

namespace umbra::pipeline {

using player::NetworkState;
using player::PlayCallback;
using player::PlayResult;
using player::PlayResultCode;
using player::ReadyState;

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
  kStalled,
  kResize,
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
  std::optional<base::ErrorDetail> detail;
  std::string track_id;
};

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
