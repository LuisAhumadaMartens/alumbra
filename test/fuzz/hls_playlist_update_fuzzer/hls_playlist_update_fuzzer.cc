#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "umbra/manifest/hls_parser.h"
#include "umbra/manifest/refresh.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  constexpr std::string_view separator = "\n---UPDATE---\n";
  const std::size_t split = input.find(separator);
  if (split == std::string_view::npos) {
    static_cast<void>(umbra::manifest::ParseHlsPlaylist(input));
    return 0;
  }
  const auto first = umbra::manifest::ParseHlsPlaylist(input.substr(0, split));
  const auto next =
      umbra::manifest::ParseHlsPlaylist(input.substr(split + separator.size()));
  if (first.ok() && next.ok()) {
    const auto previous = umbra::manifest::RefreshPresentation(nullptr, *first);
    if (previous.ok()) {
      static_cast<void>(umbra::manifest::RefreshPresentation(*previous, *next));
    }
  }
  return 0;
}
