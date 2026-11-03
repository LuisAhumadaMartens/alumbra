#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/ads/vast_macros.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const umbra::ads::VastMacroValues values = {
      {"ERRORCODE", "403"},
      {"CONTENTPLAYHEAD", "00:00:01.000"},
      {"CACHEBUSTING", "1234567890"},
      {"TIMESTAMP", "20261103T000000Z"},
  };
  const std::string expanded = umbra::ads::ExpandVastMacros(input, values);
  static_cast<void>(expanded);
  return 0;
}
