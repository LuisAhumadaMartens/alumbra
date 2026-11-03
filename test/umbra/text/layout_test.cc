#include "umbra/text/layout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace umbra::text {
namespace {

TEST(TextLayoutTest, ResolvesMixedDirectionVisualRuns) {
  const TextAnalysis analysis = AnalyzeText("English العربية 123");

  const bool has_ltr = std::ranges::any_of(
      analysis.visual_runs,
      [](const BidiRun& run) { return !run.right_to_left; });
  const bool has_rtl =
      std::ranges::any_of(analysis.visual_runs,
                          [](const BidiRun& run) { return run.right_to_left; });
  EXPECT_TRUE(has_ltr);
  EXPECT_TRUE(has_rtl);
}

TEST(TextLayoutTest, FindsCjkLineBreakOpportunity) {
  constexpr std::string_view text = "日本語の字幕";
  const TextAnalysis analysis = AnalyzeText(text);

  ASSERT_FALSE(analysis.break_offsets.empty());
  EXPECT_LT(analysis.break_offsets.front(), text.size());
}

TEST(TextLayoutTest, HandlesMixedBidiAndCjkBreaksTogether) {
  constexpr std::string_view text = "English العربية 日本語の字幕";
  const TextAnalysis analysis = AnalyzeText(text);

  EXPECT_TRUE(std::ranges::any_of(analysis.visual_runs, [](const BidiRun& run) {
    return run.right_to_left;
  }));
  EXPECT_TRUE(std::ranges::any_of(
      analysis.break_offsets,
      [&](std::size_t offset) { return offset > 0 && offset < text.size(); }));
}

}
}
