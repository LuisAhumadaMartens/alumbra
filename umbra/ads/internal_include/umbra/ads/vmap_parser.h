#ifndef UMBRA_ADS_VMAP_PARSER_H_
#define UMBRA_ADS_VMAP_PARSER_H_

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/media_time.h"
#include "umbra/ads/ad_manager.h"
#include "umbra/ads/vast_parser.h"

namespace umbra::ads {

struct VmapParseError {
  std::string break_id;
  std::string message;
};

struct VmapDocument {
  std::vector<AdBreak> breaks;
  std::vector<VmapParseError> errors;
};

using VmapErrorCallback = std::function<void(const VmapParseError&)>;

absl::StatusOr<VmapDocument> ParseVmap(std::string_view xml,
                                       penumbra::TimeDelta content_duration,
                                       VastParserOptions options = {},
                                       VmapErrorCallback on_error = {});

absl::StatusOr<VmapDocument> ParseAdSchedule(
    std::string_view xml, penumbra::TimeDelta content_duration,
    VastParserOptions options = {}, VmapErrorCallback on_error = {});

}

#endif
