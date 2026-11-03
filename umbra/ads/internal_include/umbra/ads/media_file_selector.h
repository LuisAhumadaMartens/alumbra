#ifndef UMBRA_ADS_MEDIA_FILE_SELECTOR_H_
#define UMBRA_ADS_MEDIA_FILE_SELECTOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "penumbra/interface/player.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {

enum class PreferredDelivery { kAny, kProgressive, kStreaming };

struct MediaFileSelectionOptions {
  PreferredDelivery preferred_delivery = PreferredDelivery::kAny;
  std::uint64_t max_bitrate_bps = 0;
  std::uint64_t estimated_bandwidth_bps = 0;
  std::int32_t viewport_width = 0;
  std::int32_t viewport_height = 0;
};

struct MediaFileSelection {
  std::optional<std::size_t> index;
  std::uint32_t error_code = 403;
};

[[nodiscard]] MediaFileSelection SelectMediaFile(
    const VastCreative& creative, const MediaFileSelectionOptions& options,
    const penumbra::CapabilityProvider& capabilities);

[[nodiscard]] std::optional<PreferredDelivery> ParsePreferredDelivery(
    std::string_view value);

}

#endif
