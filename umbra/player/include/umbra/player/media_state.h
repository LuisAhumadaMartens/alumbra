#ifndef UMBRA_PLAYER_MEDIA_STATE_H_
#define UMBRA_PLAYER_MEDIA_STATE_H_

#include <cstdint>

namespace umbra::player {

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

enum class Preload : uint8_t {
  kNone,
  kMetadata,
  kAuto,
};

enum class CanPlayTypeResult : uint8_t {
  kNone,
  kMaybe,
  kProbably,
};

}

#endif
