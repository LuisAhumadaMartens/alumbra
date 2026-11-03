#ifndef UMBRA_TEXT_LAYOUT_H_
#define UMBRA_TEXT_LAYOUT_H_

#include <cstddef>
#include <string_view>
#include <vector>

namespace umbra::text {

struct BidiRun {
  std::size_t offset = 0;
  std::size_t length = 0;
  bool right_to_left = false;
};

struct TextAnalysis {
  std::vector<BidiRun> visual_runs;
  std::vector<std::size_t> break_offsets;
};

TextAnalysis AnalyzeText(std::string_view text);

}

#endif
