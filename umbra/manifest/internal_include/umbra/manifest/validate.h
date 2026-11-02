#ifndef UMBRA_MANIFEST_VALIDATE_H_
#define UMBRA_MANIFEST_VALIDATE_H_

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

inline constexpr std::size_t kMaxPeriods = 65'536;
inline constexpr std::size_t kMaxSwitchingSetsPerPeriod = 4'096;
inline constexpr std::size_t kMaxTracksPerSwitchingSet = 4'096;
inline constexpr std::size_t kMaxTrickPlayTracksPerPeriod = 4'096;
inline constexpr std::size_t kMaxAdBreaks = 65'536;
inline constexpr std::size_t kMaxCodecsPerTrack = 16;
inline constexpr std::size_t kMaxStringBytes = 8'192;
inline constexpr std::size_t kMaxInitDataBytes = 65'536;
inline constexpr std::size_t kMaxProtectionEntries = 64;

[[nodiscard]] absl::Status ValidateTrack(const Track& track);
[[nodiscard]] absl::Status ValidateSwitchingSet(const SwitchingSet& set);
[[nodiscard]] absl::Status ValidatePresentation(
    const Presentation& presentation);

[[nodiscard]] absl::StatusOr<std::shared_ptr<const Presentation>>
BuildPresentation(Presentation draft);

}

#endif
