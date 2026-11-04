#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/refresh.h"
#include "umbra/manifest/validate.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const std::size_t separator = input.find('\0');
  if (separator == std::string_view::npos) {
    static_cast<void>(umbra::manifest::ParseDashMpd(input));
    return 0;
  }
  const std::string_view previous_xml = input.substr(0, separator);
  const std::string_view next_xml = input.substr(separator + 1);
  auto previous = umbra::manifest::ParseDashMpd(previous_xml);
  auto next = umbra::manifest::ParseDashMpd(next_xml);
  if (previous.ok() && next.ok()) {
    auto previous_snapshot =
        umbra::manifest::BuildPresentation(std::move(*previous));
    if (previous_snapshot.ok()) {
      static_cast<void>(umbra::manifest::RefreshPresentation(*previous_snapshot,
                                                             std::move(*next)));
    }
  }
  return 0;
}
