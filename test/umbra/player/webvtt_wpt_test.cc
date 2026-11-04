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

TEST(WebVttWptFileParsingTest, RecoversFromMalformedArrowBlocks) {
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\n\n"
      "-->\n00:00:00.000 --> 00:00:01.000\ntext0\n"
      "foo-->\n00:00:00.000 --> 00:00:01.000\ntext1\n"
      "-->foo\n00:00:00.000 --> 00:00:01.000\ntext2\n"
      "--->\n00:00:00.000 --> 00:00:01.000\ntext3\n"
      "-->-->\n00:00:00.000 --> 00:00:01.000\ntext4\n"
      "00:00:00.000 --> 00:00:01.000\ntext5\n\n"
      "00:00:00.000 -a -->\n\n00:00:00.000 --a -->\n\n"
      "00:00:00.000 - -->\n\n00:00:00.000 -- -->\n");
  ASSERT_EQ(result.cues.size(), 6);
  for (size_t index = 0; index < result.cues.size(); ++index) {
    EXPECT_EQ(result.cues[index].runs.front().text,
              "text" + std::to_string(index));
  }
}

TEST(WebVttWptFileParsingTest, RetainsMalformedTimingRecoveryCases) {
  const WebVttResult sixty = WebVttParser::Parse(
      "WEBVTT\n\n"
      "00:00:60.000 --> 00:00:01.000\ninvalid\n\n"
      "00:60:00.000 --> 00:00:01.000\ninvalid\n\n"
      "00:00:00.000 --> 00:00:60.000\ninvalid\n\n"
      "00:00:00.000 --> 00:60:00.000\ninvalid\n\n"
      "00:00:00.000 --> 60:00:01.000\ntext1\n\n"
      "60:00:00.000 --> 60:00:01.000\ntext2\n");
  ASSERT_EQ(sixty.cues.size(), 2);
  EXPECT_EQ(sixty.cues[0].start_time, 0.0);
  EXPECT_EQ(sixty.cues[0].end_time, 216001.0);
  EXPECT_EQ(sixty.cues[1].start_time, 216000.0);
  EXPECT_EQ(sixty.cues[1].end_time, 216001.0);

  const WebVttResult negative = WebVttParser::Parse(
      "WEBVTT\n\n"
      "00:00:00.000 --> 00:00:00.000\ntext0\n\n"
      "00:00:01.000 --> 00:00:00.999\ntext1\n\n"
      "00:01:00.000 --> 00:00:59.999\ntext2\n\n"
      "01:00:00.000 --> 00:59:59.999\ntext3\n");
  ASSERT_EQ(negative.cues.size(), 4);
  EXPECT_EQ(negative.cues[1].start_time, 1.0);
  EXPECT_EQ(negative.cues[1].end_time, 0.999);
  EXPECT_EQ(negative.cues[2].start_time, 60.0);
  EXPECT_EQ(negative.cues[2].end_time, 59.999);
  EXPECT_EQ(negative.cues[3].start_time, 3600.0);
  EXPECT_EQ(negative.cues[3].end_time, 3599.999);

  const std::vector<std::string> invalid = {"00:00:00.000 -->",
                                            "x00:00:00.000 --> 00:00:01.000",
                                            "00:00:00.000 -x-> 00:00:01.000",
                                            "00:00:00.000 --> 00:00:01.x00",
                                            "00:00:00,000 --> 00:00:01,000",
                                            "00::00:00.000 --> 00:00:01.000",
                                            "00:00:00.0000 --> 00:00:01.000",
                                            "00:00:0.000 --> 00:00:01.000"};
  for (const std::string& timing : invalid) {
    SCOPED_TRACE(timing);
    const WebVttResult result =
        WebVttParser::Parse("WEBVTT\n\n" + timing + "\ninvalid\n");
    EXPECT_TRUE(result.cues.empty());
  }

  const WebVttResult long_hours = WebVttParser::Parse(
      "WEBVTT\n\n000:00:00.000 --> 00:00:01.000\ntext0\n\n"
      "00:00:00.000 --> 000:00:01.000\ntext1\n");
  ASSERT_EQ(long_hours.cues.size(), 2);
  EXPECT_EQ(long_hours.cues[0].end_time, 1.0);
  EXPECT_EQ(long_hours.cues[1].end_time, 1.0);
}

TEST(WebVttWptFileParsingTest, HandlesSignatureRecoveryCases) {
  const std::vector<FileParsingCase> valid = {
      {"signature-no-newline.html", "WEBVTT", 0, ""},
      {"signature-space-no-newline.html", "WEBVTT ", 0, ""},
      {"signature-space.html", "WEBVTT \n", 0, ""},
      {"signature-tab-no-newline.html", "WEBVTT\t", 0, ""},
      {"signature-tab.html", "WEBVTT\t\n", 0, ""},
      {"signature-timings.html", "WEBVTT 00:00:00.000 --> 00:00:01.000\ntext\n",
       0, ""},
  };
  for (const FileParsingCase& test_case : valid) {
    SCOPED_TRACE(test_case.source);
    const WebVttResult result = WebVttParser::Parse(test_case.input);
    EXPECT_TRUE(result.cues.empty());
    EXPECT_TRUE(result.warnings.empty());
  }

  const std::vector<FileParsingCase> invalid = {
      {"signature-invalid.html", "VTTWEB\n", 0, ""},
      {"support/signature-formfeed.vtt", "WEBVTT\f\n", 0, ""},
      {"support/signature-invalid-whitespace.vtt",
       "WEBVTT\xC2\xA0"
       "foo\n",
       0, ""},
      {"support/signature-invalid.vtt", "VTTWEB\n", 0, ""},
      {"support/signature-lowercase.vtt", "webvtt\n", 0, ""},
      {"support/signature-missing-whitespace.vtt", "WEBVTTfoo\n", 0, ""},
      {"support/signature-missing.vtt",
       "00:00:00.000 --> 00:00:01.000\ninvalid\n", 0, ""},
      {"support/signature-partial.vtt", "WEBVT", 0, ""},
      {"support/signature-websrt.vtt", "WEBSRT\n", 0, ""},
  };
  for (const FileParsingCase& test_case : invalid) {
    SCOPED_TRACE(test_case.source);
    const WebVttResult result = WebVttParser::Parse(test_case.input);
    EXPECT_TRUE(result.cues.empty());
    EXPECT_FALSE(result.warnings.empty());
  }
}

TEST(WebVttWptFileParsingTest, RecoversHeadersAndLineTerminators) {
  const std::vector<FileParsingCase> headers = {
      {"header-space.html", "WEBVTT\n \n00:00:00.000 --> 00:00:01.000\ntext\n",
       1, "text"},
      {"header-tab.html", "WEBVTT\n\t\n00:00:00.000 --> 00:00:01.000\ntext\n",
       1, "text"},
      {"header-timings.html", "WEBVTT\n00:00:00.000 --> 00:00:01.000\ntext\n",
       1, "text"},
      {"whitespace-chars.html",
       "WEBVTT\n\nform "
       "feed\n\f\f\f00:00:00.000\f\f\f\f-->\f\f00:00:01.000\f\ntext\n",
       1, "text"},
  };
  for (const FileParsingCase& test_case : headers) {
    SCOPED_TRACE(test_case.source);
    const WebVttResult result = WebVttParser::Parse(test_case.input);
    ASSERT_EQ(result.cues.size(), test_case.cue_count);
    ASSERT_FALSE(result.cues.front().runs.empty());
    EXPECT_EQ(result.cues.front().runs.front().text, test_case.first_text);
  }

  const WebVttResult newlines = WebVttParser::Parse(
      "WEBVTT\r\rcr\r00:00:00.000 --> 00:00:01.000\rtext0\n\n"
      "lf\n00:00:00.000 --> 00:00:01.000\ntext1\r\n\r\n"
      "crlf\r\n00:00:00.000 --> 00:00:01.000\r\ntext2\n\r"
      "lfcr\r00:00:00.000 --> 00:00:01.000\ntext3\n\r");
  ASSERT_EQ(newlines.cues.size(), 4);
  EXPECT_EQ(newlines.cues[0].runs.front().text, "text0");
  EXPECT_EQ(newlines.cues[1].runs.front().text, "text1");
  EXPECT_EQ(newlines.cues[2].runs.front().text, "text2");
  EXPECT_EQ(newlines.cues[3].runs.front().text, "text3");
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
