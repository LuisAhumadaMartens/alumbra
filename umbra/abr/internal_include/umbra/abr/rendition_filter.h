#ifndef UMBRA_ABR_RENDITION_FILTER_H_
#define UMBRA_ABR_RENDITION_FILTER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/player.h"
#include "umbra/manifest/presentation.h"
#include "umbra/settings/store.h"

namespace umbra::abr {

enum class FilterReason {
  kUnsupported,
  kNotSmooth,
  kManualSelection,
  kMaximumHeight,
  kMaximumBitrate,
  kDataSaver,
  kCmsd,
  kDecodeDropCap,
  kViewport,
};

struct FilterPolicy {
  std::int32_t max_height = 0;
  std::int64_t max_bitrate_bps = 0;
  std::optional<std::int64_t> data_saver_max_bitrate_bps;
  std::int64_t cmsd_max_bitrate_bps = 0;
  std::int32_t decode_drop_max_height = 0;
  bool cap_to_viewport = true;
  std::int32_t viewport_height_pixels = 0;
  std::optional<std::string> manual_selection;
};

struct RenditionDescriptor {
  std::string id;
  std::int32_t height = 0;
  std::int64_t bitrate_bps = 0;
};

struct RenditionCapability {
  std::string id;
  bool supported = false;
  bool smooth = false;
};

struct ExcludedRendition {
  std::string id;
  FilterReason reason = FilterReason::kUnsupported;
};

struct FilterResult {
  std::vector<std::size_t> eligible_indices;
  std::vector<ExcludedRendition> excluded;
};

[[nodiscard]] FilterPolicy ReadFilterPolicy(
    const settings::Snapshot& load_settings, bool cap_to_viewport,
    std::int32_t viewport_height_pixels,
    std::optional<std::string> manual_selection = std::nullopt);

[[nodiscard]] absl::StatusOr<std::vector<RenditionCapability>>
QueryRenditionCapabilities(std::span<const manifest::Track* const> tracks,
                           const penumbra::CapabilityProvider& capabilities);

[[nodiscard]] absl::StatusOr<FilterResult> FilterRenditions(
    std::span<const RenditionDescriptor> renditions,
    std::span<const RenditionCapability> capabilities,
    const FilterPolicy& policy);

}

#endif
