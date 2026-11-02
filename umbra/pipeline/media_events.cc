#include "umbra/pipeline/media_events.h"

#include <string_view>

namespace umbra::pipeline {

std::string_view MediaEventName(MediaEventType type) {
  switch (type) {
    case MediaEventType::kLoadStart:
      return "loadstart";
    case MediaEventType::kProgress:
      return "progress";
    case MediaEventType::kSuspend:
      return "suspend";
    case MediaEventType::kAbort:
      return "abort";
    case MediaEventType::kError:
      return "error";
    case MediaEventType::kEmptied:
      return "emptied";
    case MediaEventType::kLoadedMetadata:
      return "loadedmetadata";
    case MediaEventType::kLoadedData:
      return "loadeddata";
    case MediaEventType::kCanPlay:
      return "canplay";
    case MediaEventType::kCanPlayThrough:
      return "canplaythrough";
    case MediaEventType::kPlaying:
      return "playing";
    case MediaEventType::kWaiting:
      return "waiting";
    case MediaEventType::kSeeking:
      return "seeking";
    case MediaEventType::kSeeked:
      return "seeked";
    case MediaEventType::kEnded:
      return "ended";
    case MediaEventType::kDurationChange:
      return "durationchange";
    case MediaEventType::kTimeUpdate:
      return "timeupdate";
    case MediaEventType::kPlay:
      return "play";
    case MediaEventType::kPause:
      return "pause";
    case MediaEventType::kRateChange:
      return "ratechange";
    case MediaEventType::kVolumeChange:
      return "volumechange";
  }
  return "unknown";
}

std::string_view NetworkStateName(NetworkState state) {
  switch (state) {
    case NetworkState::kEmpty:
      return "NETWORK_EMPTY";
    case NetworkState::kIdle:
      return "NETWORK_IDLE";
    case NetworkState::kLoading:
      return "NETWORK_LOADING";
    case NetworkState::kNoSource:
      return "NETWORK_NO_SOURCE";
  }
  return "unknown";
}

std::string_view ReadyStateName(ReadyState state) {
  switch (state) {
    case ReadyState::kHaveNothing:
      return "HAVE_NOTHING";
    case ReadyState::kHaveMetadata:
      return "HAVE_METADATA";
    case ReadyState::kHaveCurrentData:
      return "HAVE_CURRENT_DATA";
    case ReadyState::kHaveFutureData:
      return "HAVE_FUTURE_DATA";
    case ReadyState::kHaveEnoughData:
      return "HAVE_ENOUGH_DATA";
  }
  return "unknown";
}

}
