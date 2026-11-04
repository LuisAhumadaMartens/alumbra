#include "umbra/player/ass_parser.h"

#include <gtest/gtest.h>

#include <string_view>

#include "umbra/player/text_track.h"

namespace umbra::player {
namespace {

constexpr std::string_view kScript =
    "[Script Info]\n"
    "PlayResX: 1280\n"
    "PlayResY: 720\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, Bold, Italic, Underline, "
    "Alignment\n"
    "Style: Main,Arial,40,&H0000FF00,1,0,0,2\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
    "Effect, Text\n"
    "Dialogue: "
    "0,0:00:01.00,0:00:04.00,Main,,0,0,0,,{\\pos(640,600)}Hello\\Nworld\n";

TEST(AssParserTest, ParsesStylePositionColourAndLineBreaks) {
  const AssResult result = AssParser::Parse(kScript);

  ASSERT_EQ(result.cues.size(), 1U)
      << (result.warnings.empty() ? "no warning"
                                  : result.warnings.front().reason);
  const TextCue& cue = result.cues.front();
  ASSERT_EQ(cue.runs.size(), 1U);
  EXPECT_EQ(cue.runs.front().text, "Hello\nworld");
  EXPECT_EQ(cue.runs.front().font_family, "Arial");
  EXPECT_DOUBLE_EQ(cue.runs.front().font_size, 40.0);
  EXPECT_TRUE(cue.runs.front().bold);
  EXPECT_TRUE(cue.runs.front().has_foreground_color);
  EXPECT_EQ(cue.runs.front().foreground_argb, 0xFF00FF00U);
  EXPECT_DOUBLE_EQ(cue.position, 50.0);
  EXPECT_DOUBLE_EQ(cue.line, 600.0 * 100.0 / 720.0);
  EXPECT_EQ(result.play_res_x, 1280);
}

TEST(AssParserTest, ParsesFadeAndDegradesUnsupportedTags) {
  constexpr std::string_view script =
      "[Events]\n"
      "Format: Start,End,Style,Text\n"
      "Dialogue: "
      "0:00:00.00,0:00:02.00,Default,{\\fad(250,500)\\t(0,500,\\fs48)}Text\n";
  const AssResult result = AssParser::Parse(script);

  ASSERT_EQ(result.cues.size(), 1U);
  EXPECT_DOUBLE_EQ(result.cues.front().fade_in_seconds, 0.25);
  EXPECT_DOUBLE_EQ(result.cues.front().fade_out_seconds, 0.5);
  ASSERT_EQ(result.cues.front().runs.size(), 1U);
  EXPECT_EQ(result.cues.front().runs.front().text, "Text");
  ASSERT_FALSE(result.warnings.empty());
}

TEST(AssParserTest, ParsesMatroskaEventUsingCodecPrivateHeader) {
  constexpr std::string_view header =
      "[Script Info]\nPlayResX: 640\nPlayResY: 360\n"
      "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, Bold, "
      "Italic, Underline, Alignment\n"
      "Style: Default,Arial,32,&H00FFFFFF,0,0,0,2\n"
      "[Events]\nFormat: Layer, Style, Name, MarginL, MarginR, MarginV, "
      "Effect, Text\n";
  const AssResult result = AssParser::ParseMatroskaEvent(
      header, "0,Default,,0,0,0,,Attached font line", 3.0, 5.0);

  ASSERT_EQ(result.cues.size(), 1U)
      << (result.warnings.empty() ? "no warning"
                                  : result.warnings.front().reason);
  EXPECT_DOUBLE_EQ(result.cues.front().start_time, 3.0);
  EXPECT_DOUBLE_EQ(result.cues.front().end_time, 5.0);
  ASSERT_EQ(result.cues.front().runs.size(), 1U);
  EXPECT_EQ(result.cues.front().runs.front().text, "Attached font line");
}

TEST(AssParserTest, BoundsScriptAndEventInput) {
  AssOptions options;
  options.max_script_bytes = 4;
  EXPECT_TRUE(AssParser::Parse("[Events]", options).cues.empty());
}

}
}
