#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/player/webvtt_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const umbra::player::WebVttResult expected =
      umbra::player::WebVttParser::Parse(input);
  static_cast<void>(
      umbra::player::WebVttParser::ParseWvttSample(input, 0.0, 1.0));
  umbra::player::WebVttParser parser;
  std::size_t cue_count = 0;
  const auto on_cue = [&cue_count](auto&& cue) {
    static_cast<void>(cue);
    ++cue_count;
  };
  const std::span<const std::uint8_t> bytes(data, size);
  const std::size_t split = size == 0 ? 0 : bytes.front() % (size + 1);
  parser.Feed(input.substr(0, split), on_cue);
  parser.Feed(input.substr(split), on_cue);
  const umbra::player::WebVttResult result = parser.Finish();
  if (cue_count != expected.cues.size() ||
      result.warnings.size() != expected.warnings.size()) {
    std::abort();
  }
  return 0;
}
