#ifndef UMBRA_ADS_MANIFEST_BRIDGE_H_
#define UMBRA_ADS_MANIFEST_BRIDGE_H_

#include <optional>

#include "umbra/ads/ad_manager.h"
#include "umbra/manifest/presentation.h"

namespace umbra::ads {

[[nodiscard]] std::optional<AdBreak> FromManifestCue(
    const umbra::manifest::AdBreak& cue);

}

#endif
