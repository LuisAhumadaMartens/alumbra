#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/player/srt_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const umbra::player::internal::SrtParseResult result =
      umbra::player::internal::ParseSubRip(input);
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
