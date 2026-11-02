#ifndef UMBRA_PLAYER_PLAY_RESULT_H_
#define UMBRA_PLAYER_PLAY_RESULT_H_

#include <cstdint>
#include <functional>
#include <optional>

#include "umbra/base/error_detail.h"

namespace umbra::player {

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

}

#endif
