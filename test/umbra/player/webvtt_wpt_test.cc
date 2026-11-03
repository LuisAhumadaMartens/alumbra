#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/player/text_track.h"
#include "umbra/player/webvtt_parser.h"
namespace umbra::player {
namespace {

struct FileParsingCase {
  const char* source;
  const char* input;
  size_t cue_count;
  const char* first_text;
};

TEST(WebVttWptFileParsingTest, ParserCases) {
  const std::vector<FileParsingCase> cases = {
      {"support/header-garbage.test",
       "WEBVTT garbage\n\n00:00.000 --> 00:01.000\ntext\n", 1, "text"},
      {"support/ids.test",
       "WEBVTT\n\nfirst\n00:00.000 --> 00:01.000\none\n\nsecond\n00:01.000 --> "
       "00:02.000\ntwo\n",
       2, "one"},
      {"support/timings-omitted-hours.test",
       "WEBVTT\n\n00:01.000 --> 00:02.000\ntext\n", 1, "text"},
      {"support/settings-position.test",
       "WEBVTT\n\n00:00.000 --> 00:01.000 position:25%\ntext\n", 1, "text"},
      {"support/settings-size.test",
       "WEBVTT\n\n00:00.000 --> 00:01.000 size:75%\ntext\n", 1, "text"},
      {"support/settings-vertical.test",
       "WEBVTT\n\n00:00.000 --> 00:01.000 vertical:rl\ntext\n", 1, "text"},
      {"support/comment-in-cue-text.test",
       "WEBVTT\n\n00:00.000 --> 00:01.000\n<!-- comment -->text\n", 1, "text"},
      {"support/regions-id.test",
       "WEBVTT\n\nREGION\nid:bottom\nwidth:40%\n\n00:00.000 --> 00:01.000 "
       "region:bottom\ntext\n",
       1, "text"},
  };
  for (const FileParsingCase& test_case : cases) {
    SCOPED_TRACE(test_case.source);
    const WebVttResult result = WebVttParser::Parse(test_case.input);
    ASSERT_EQ(result.cues.size(), test_case.cue_count);
    ASSERT_FALSE(result.cues.front().runs.empty());
    EXPECT_EQ(result.cues.front().runs.front().text, test_case.first_text);
  }
}

TEST(WebVttWptFileParsingTest, CueTextTagsMapToStyledRuns) {
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\n\n00:00.000 --> 00:01.000\n"
      "<c.red.blue>colour</c> <b>bold <i>both</i></b> <v Speaker>voice</v> "
      "<lang en>English</lang> <ruby>base<rt>reading</rt></ruby> "
      "&#x1F600; &amp; <00:00:00.500>end\n");
  ASSERT_EQ(result.cues.size(), 1);
  ASSERT_GE(result.cues[0].runs.size(), 6);
  EXPECT_EQ(result.cues[0].runs[0].classes,
            (std::vector<std::string>{"red", "blue"}));
  EXPECT_TRUE(std::any_of(result.cues[0].runs.begin(),
                          result.cues[0].runs.end(),
                          [](const StyledTextRun& run) { return run.bold; }));
  EXPECT_TRUE(std::any_of(
      result.cues[0].runs.begin(), result.cues[0].runs.end(),
      [](const StyledTextRun& run) { return run.bold && run.italic; }));
  EXPECT_TRUE(std::any_of(
      result.cues[0].runs.begin(), result.cues[0].runs.end(),
      [](const StyledTextRun& run) { return run.voice == "Speaker"; }));
  EXPECT_TRUE(std::any_of(
      result.cues[0].runs.begin(), result.cues[0].runs.end(),
      [](const StyledTextRun& run) { return run.language == "en"; }));
  EXPECT_EQ(result.cues[0].ruby_annotation, "reading");
  EXPECT_TRUE(std::any_of(result.cues[0].runs.begin(),
                          result.cues[0].runs.end(),
                          [](const StyledTextRun& run) {
                            return run.has_timestamp && run.timestamp == 0.5;
                          }));
}

TEST(WebVttWptFileParsingTest, CueSettingsRetainAlignmentAndLineMode) {
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\n\n"
      "00:00.000 --> 00:01.000 align:start position:25%,line-left\na\n\n"
      "00:01.000 --> 00:02.000 align:right line:50%,center\nb\n");
  ASSERT_EQ(result.cues.size(), 2);
  EXPECT_EQ(result.cues[0].alignment, "start");
  EXPECT_EQ(result.cues[0].position, 25.0);
  EXPECT_EQ(result.cues[0].position_alignment, "line-left");
  EXPECT_TRUE(result.cues[0].snap_to_lines);
  EXPECT_EQ(result.cues[1].alignment, "right");
  EXPECT_EQ(result.cues[1].line, 50.0);
  EXPECT_FALSE(result.cues[1].snap_to_lines);
  EXPECT_EQ(result.cues[1].line_alignment, "center");
}

TEST(WebVttWptFileParsingTest, RejectsCueTextBeyondNestingLimit) {
  WebVttOptions options;
  options.max_nesting_depth = 2;
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\n\n00:00.000 --> 00:01.000\n<b><i><u>x</u></i></b>\n", options);
  EXPECT_TRUE(result.cues.empty());
  ASSERT_EQ(result.warnings.size(), 1);
  EXPECT_EQ(result.warnings[0].reason, "cue text nesting limit exceeded");
}

TEST(WebVttWptFileParsingTest, EmitsCueWhenItsBlockCompletes) {
  WebVttParser parser;
  size_t count = 0;
  const auto callback = [&count](TextCue cue) {
    EXPECT_EQ(cue.runs.front().text, "ready");
    ++count;
  };
  parser.Feed("WEBVTT\n\n00:00.000 --> 00:01.000\nready\n\n", callback);
  EXPECT_EQ(count, 1);
  EXPECT_TRUE(parser.Finish().cues.empty());
}

}
}
