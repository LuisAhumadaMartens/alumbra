#ifndef UMBRA_PLAYER_CAN_PLAY_TYPE_H_
#define UMBRA_PLAYER_CAN_PLAY_TYPE_H_

#include <string_view>

#include "penumbra/interface/player.h"
#include "umbra/player/media_state.h"

namespace umbra::player::internal {

[[nodiscard]] CanPlayTypeResult AnswerCanPlayType(
    std::string_view type, const penumbra::CapabilityProvider* capabilities);

}

#endif
