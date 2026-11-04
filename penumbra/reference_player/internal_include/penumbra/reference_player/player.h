#ifndef PENUMBRA_REFERENCE_PLAYER_PLAYER_H_
#define PENUMBRA_REFERENCE_PLAYER_PLAYER_H_

#include <memory>

#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"

namespace penumbra {

absl::StatusOr<std::unique_ptr<Player>> CreateReferencePlayer(
    PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks, Port& port,
    std::shared_ptr<ControlTaskRunner> control);

}

#endif
