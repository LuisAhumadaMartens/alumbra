#include <cstddef>
#include <cstdint>
#include <string_view>

#include "umbra/manifest/hls_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  umbra::manifest::HlsParserOptions options;
  options.base_uri = "https://example.test/hls/master.m3u8?token=seed";
  options.query_parameters.emplace("token", "seed");
  options.imported_variables.emplace("host", "media.example.test");
  static_cast<void>(umbra::manifest::ParseHlsPlaylist(input, options));
  return 0;
}
