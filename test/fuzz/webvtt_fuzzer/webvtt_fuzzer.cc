#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/player/webvtt_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  static_cast<void>(umbra::player::WebVttParser::Parse(input));
  static_cast<void>(
      umbra::player::WebVttParser::ParseWvttSample(input, 0.0, 1.0));
  return 0;
}
