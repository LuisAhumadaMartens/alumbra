#ifndef UMBRA_MANIFEST_DASH_MPD_PARSER_H_
#define UMBRA_MANIFEST_DASH_MPD_PARSER_H_

#include <cstddef>
#include <string_view>

#include "absl/status/statusor.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

inline constexpr std::size_t kDefaultDashMpdMaxBytes = 16'777'216;

[[nodiscard]] absl::StatusOr<Presentation> ParseDashMpd(
    std::string_view xml, std::size_t max_bytes = kDefaultDashMpdMaxBytes);

}

#endif
