#include <cstddef>
#include <cstdint>
#include <string_view>

#include "penumbra/interface/media_time.h"
#include "umbra/ads/vmap_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  static_cast<void>(
      umbra::ads::ParseVmap(input, penumbra::TimeDelta::FromSeconds(600)));
  static_cast<void>(umbra::ads::ParseAdSchedule(
      input, penumbra::TimeDelta::FromSeconds(600)));
  return 0;
}
