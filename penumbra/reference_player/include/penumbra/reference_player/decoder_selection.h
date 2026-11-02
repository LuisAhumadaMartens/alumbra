#ifndef PENUMBRA_REFERENCE_PLAYER_DECODER_SELECTION_H_
#define PENUMBRA_REFERENCE_PLAYER_DECODER_SELECTION_H_

#include <span>
#include <vector>

#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/reference_player/decoder_blocklist.h"

namespace penumbra {

struct SelectionRequest {
  StreamConfig stream;
  bool require_secure = false;
  DecoderSettings settings;
  DeviceProfile device;
  std::span<const BlocklistEntry> blocklist;
};

std::vector<DecoderCandidate> SelectDecoderCandidates(
    std::vector<DecoderCandidate> available, const SelectionRequest& request);

}

#endif
