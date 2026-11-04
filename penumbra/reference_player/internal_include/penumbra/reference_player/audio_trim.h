#ifndef PENUMBRA_REFERENCE_PLAYER_AUDIO_TRIM_H_
#define PENUMBRA_REFERENCE_PLAYER_AUDIO_TRIM_H_

#include <optional>

#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"

namespace penumbra::reference_player {

[[nodiscard]] std::optional<DecodedAudioBuffer> TrimAudioBuffer(
    const DecodedAudioBuffer& source, MediaTime target, bool* reached_target);

}

#endif
