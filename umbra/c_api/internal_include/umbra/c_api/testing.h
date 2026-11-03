#ifndef UMBRA_C_API_TESTING_H_
#define UMBRA_C_API_TESTING_H_

#include <functional>
#include <memory>

#include "umbra/player/player.h"

namespace umbra::c_api::testing {

using PlayerCreator =
    std::function<std::unique_ptr<player::Player>(player::PlayerOptions)>;

void SetPlayerCreator(PlayerCreator creator);

}

#endif
