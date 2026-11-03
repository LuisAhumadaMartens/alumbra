#ifndef UMBRA_MANIFEST_HLS_PARSER_H_
#define UMBRA_MANIFEST_HLS_PARSER_H_

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

struct HlsAttribute {
  std::string name;
  std::string value;
  bool quoted = false;
};

struct HlsParserOptions {
  std::string base_uri;
  std::map<std::string, std::string, std::less<>> query_parameters;
  std::map<std::string, std::string, std::less<>> imported_variables;
  bool strict = false;
};

[[nodiscard]] absl::StatusOr<std::vector<HlsAttribute>> ParseHlsAttributeList(
    std::string_view input);

[[nodiscard]] absl::StatusOr<Presentation> ParseHlsPlaylist(
    std::string_view input, const HlsParserOptions& options = {});

[[nodiscard]] absl::Status SetHlsPresentationTypeFromMediaPlaylists(
    Presentation& master, std::span<const PresentationType> media_types);

}

#endif
