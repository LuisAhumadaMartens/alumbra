#include <cstddef>
#include <cstdint>
#include <string_view>

#include "umbra/manifest/hls_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  static_cast<void>(umbra::manifest::ParseHlsAttributeList(input));
  return 0;
}
