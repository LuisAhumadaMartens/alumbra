#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/player/ass_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const umbra::player::AssResult parsed =
      umbra::player::AssParser::Parse(input);
  const std::size_t separator = input.find('\0');
  const std::string_view header = input.substr(0, separator);
  const std::string_view event =
      separator == std::string_view::npos ? input : input.substr(separator + 1);
  const umbra::player::AssResult block =
      umbra::player::AssParser::ParseMatroskaEvent(header, event, 0.5, 1.5);
  for (const umbra::player::AssResult* result : {&parsed, &block}) {
    if (result->cues.size() > 100000) {
      std::abort();
    }
    for (const umbra::player::TextCue& cue : result->cues) {
      if (cue.start_time < 0.0 || cue.end_time <= cue.start_time) {
        std::abort();
      }
      for (const umbra::player::StyledTextRun& run : cue.runs) {
        if (run.text.size() > size + 1024) {
          std::abort();
        }
      }
    }
  }
  return 0;
}
