#ifndef UMBRA_PLAYER_PORT_ACCESS_H_
#define UMBRA_PLAYER_PORT_ACCESS_H_

#include <memory>

#include "penumbra/interface/port.h"

namespace umbra::player {

class Player;

namespace internal {

[[nodiscard]] std::shared_ptr<penumbra::Port> GetPort(Player& player);

}

}

#endif
