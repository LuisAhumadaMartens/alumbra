#include "umbra/text/layout.h"

#include <SheenBidi/SBAlgorithm.h>
#include <SheenBidi/SBBase.h>
#include <SheenBidi/SBCodepointSequence.h>
#include <SheenBidi/SBLine.h>
#include <SheenBidi/SBParagraph.h>
#include <SheenBidi/SBRun.h>
#include <linebreak.h>
#include <unibreakbase.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace umbra::text {
namespace {

struct AlgorithmDeleter {
  void operator()(const _SBAlgorithm* algorithm) const {
    SBAlgorithmRelease(algorithm);
  }
};

struct ParagraphDeleter {
  void operator()(const _SBParagraph* paragraph) const {
    SBParagraphRelease(paragraph);
  }
};

struct LineDeleter {
  void operator()(const _SBLine* line) const { SBLineRelease(line); }
};

}

TextAnalysis AnalyzeText(std::string_view text) {
  TextAnalysis result;
  if (text.empty() || text.size() > std::numeric_limits<SBUInteger>::max()) {
    return result;
  }

  const SBCodepointSequence sequence = {
      .stringEncoding = SBStringEncodingUTF8,
      .stringBuffer = text.data(),
      .stringLength = static_cast<SBUInteger>(text.size()),
  };
  const std::unique_ptr<const _SBAlgorithm, AlgorithmDeleter> algorithm(
      SBAlgorithmCreate(&sequence));
  if (!algorithm) {
    return result;
  }

  SBUInteger paragraph_length = 0;
  SBUInteger separator_length = 0;
  SBAlgorithmGetParagraphBoundary(algorithm.get(), 0, sequence.stringLength,
                                  &paragraph_length, &separator_length);
  const std::unique_ptr<const _SBParagraph, ParagraphDeleter> paragraph(
      SBAlgorithmCreateParagraph(algorithm.get(), 0, paragraph_length,
                                 SBLevelDefaultLTR));
  if (paragraph) {
    const std::unique_ptr<const _SBLine, LineDeleter> line(
        SBParagraphCreateLine(paragraph.get(), 0,
                              SBParagraphGetLength(paragraph.get())));
    if (line) {
      const SBUInteger count = SBLineGetRunCount(line.get());
      const std::span<const SBRun> runs(SBLineGetRunsPtr(line.get()), count);
      result.visual_runs.reserve(count);
      for (const SBRun& run : runs) {
        result.visual_runs.push_back({.offset = run.offset,
                                      .length = run.length,
                                      .right_to_left = (run.level & 1U) != 0});
      }
    }
  }

  std::vector<char> breaks(text.size() + 1);
  set_linebreaks_utf8_per_code_point(
      reinterpret_cast<const utf8_t*>(text.data()), text.size(), nullptr,
      breaks.data());
  for (std::size_t offset = 0; offset < breaks.size(); ++offset) {
    if (breaks[offset] == LINEBREAK_ALLOWBREAK ||
        breaks[offset] == LINEBREAK_MUSTBREAK) {
      result.break_offsets.push_back(offset);
    }
  }
  return result;
}

}
