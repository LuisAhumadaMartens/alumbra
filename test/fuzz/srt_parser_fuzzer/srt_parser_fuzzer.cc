#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/player/srt_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  umbra::player::internal::SrtParseOptions options;
  options.fallback_encoding = "windows-1252";
  options.auto_detect = true;
  umbra::player::internal::SrtParser parser(options);
  const std::span<const std::uint8_t> bytes(data, size);
  const std::size_t split = size == 0 ? 0 : bytes.front() % (size + 1);
  if (!parser.Feed(input.substr(0, split)) ||
      !parser.Feed(input.substr(split))) {
    return 0;
  }
  const umbra::player::internal::SrtParseResult result = parser.Finish();
  if (!result.decoded) {
    return 0;
  }
  double previous_start = 0.0;
  for (const auto& cue : result.cues) {
    if (cue.start_time < previous_start || cue.end_time < cue.start_time ||
        cue.runs.empty()) {
      std::abort();
    }
    previous_start = cue.start_time;
  }
  return 0;
}
