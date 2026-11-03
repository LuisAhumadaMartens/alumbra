#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "umbra/ads/vast_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  static_cast<void>(umbra::ads::ParseVast(input));
  static_cast<void>(umbra::ads::ResolveVastWrappers(
      input,
      [input](std::string_view, std::uint32_t) -> absl::StatusOr<std::string> {
        return std::string(input);
      }));
  return 0;
}
