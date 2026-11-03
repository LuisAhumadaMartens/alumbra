#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/text/ttml_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  umbra::text::TtmlParserOptions options;
  options.max_document_bytes = static_cast<std::size_t>(4U * 1024U * 1024U);
  options.max_depth = 64;
  options.max_cues = 10000;
  static_cast<void>(umbra::text::ParseTtml(
      std::string_view(reinterpret_cast<const char*>(data), size), options));
  return 0;
}
