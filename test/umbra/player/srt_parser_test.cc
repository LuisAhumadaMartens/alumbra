#include "umbra/player/srt_parser.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "umbra/player/text_track.h"

namespace umbra::player::internal {
namespace {

std::string EncodeUtf16(std::string_view input, bool little_endian) {
  std::string output;
  output.push_back(static_cast<char>(little_endian ? 0xff : 0xfe));
  output.push_back(static_cast<char>(little_endian ? 0xfe : 0xff));
  for (const unsigned char byte : input) {
    if (little_endian) {
      output.push_back(static_cast<char>(byte));
      output.push_back('\0');
    } else {
      output.push_back('\0');
      output.push_back(static_cast<char>(byte));
    }
  }
  return output;
}

TEST(SrtParserTest, ParsesOptionalIndicesLineEndingsAndOverlappingCues) {
  const SrtParseResult result = ParseSubRip(
      "SRT preamble\r\n\r\n"
      "00:00:02.000 --> 00:00:04,000\r"
      "later cue\r\n\r\n"
      "9\n00:00:01,000 --> 00:00:03.000\nfirst cue\n\n"
      "garbage after cues");
  ASSERT_EQ(result.cues.size(), 2U);
  EXPECT_DOUBLE_EQ(result.cues[0].start_time, 1.0);
  EXPECT_DOUBLE_EQ(result.cues[1].start_time, 2.0);
  EXPECT_EQ(result.cues[0].runs.front().text, "first cue");
  EXPECT_EQ(result.cues[1].runs.front().text, "later cue");
}

TEST(SrtParserTest, ParsesStyledRunsEntitiesFontTagsAndAlignment) {
  const SrtParseResult result = ParseSubRip(
      "1\n00:00:00,000 --> 00:00:02,000\n"
      "{\\an7}<i>one <b>two</b></i> <u>three</u> &amp; "
      "<font color=\"red\">four</font>");
  ASSERT_EQ(result.cues.size(), 1U);
  const SrtCue& cue = result.cues.front();
  std::string all_text;
  for (const SrtStyledTextRun& run : cue.runs) {
    all_text += run.text;
  }
  EXPECT_TRUE(std::ranges::any_of(cue.runs, [](const SrtStyledTextRun& run) {
    return run.italic && !run.bold && run.text.find("one") != std::string::npos;
  }));
  EXPECT_TRUE(std::ranges::any_of(cue.runs, [](const SrtStyledTextRun& run) {
    return run.italic && run.bold && run.text.find("two") != std::string::npos;
  }));
  EXPECT_TRUE(std::ranges::any_of(cue.runs, [](const SrtStyledTextRun& run) {
    return run.underline && run.text.find("three") != std::string::npos;
  }));
  EXPECT_NE(all_text.find("& four"), std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(cue.runs, [](const SrtStyledTextRun& run) {
    return run.color == "red" && run.text == "four";
  }));
  EXPECT_EQ(all_text.find("{\\an"), std::string::npos);
  EXPECT_EQ(cue.line, 0.0);
  EXPECT_EQ(cue.position, 0.0);
}

TEST(SrtParserTest, DecodesUtf8AndUtf16ByteOrderMarks) {
  const std::string utf8 =
      "\xef\xbb\xbf"
      "1\n00:00:00,000 --> 00:00:01,000\n\xc3\xa9";
  const SrtParseResult utf8_result = ParseSubRip(utf8);
  ASSERT_EQ(utf8_result.cues.size(), 1U);
  EXPECT_EQ(utf8_result.detected_encoding, "utf-8");
  EXPECT_DOUBLE_EQ(utf8_result.encoding_confidence, 1.0);
  EXPECT_EQ(utf8_result.cues.front().runs.front().text, "\xc3\xa9");

  const std::string srt = "1\n00:00:00,000 --> 00:00:01,000\ncaption";
  const SrtParseResult le_result = ParseSubRip(EncodeUtf16(srt, true));
  ASSERT_EQ(le_result.cues.size(), 1U);
  EXPECT_EQ(le_result.detected_encoding, "utf-16le");

  const SrtParseResult be_result = ParseSubRip(EncodeUtf16(srt, false));
  ASSERT_EQ(be_result.cues.size(), 1U);
  EXPECT_EQ(be_result.detected_encoding, "utf-16be");
}

TEST(SrtParserTest, UsesFallbackEncodingAndSupportsPerParseOverride) {
  const std::string input = "1\n00:00:00,000 --> 00:00:01,000\n\xe9";
  SrtParseOptions options;
  options.fallback_encoding = "iso-8859-1";
  const SrtParseResult fallback = ParseSubRip(input, options);
  ASSERT_EQ(fallback.cues.size(), 1U);
  EXPECT_EQ(fallback.detected_encoding, "iso-8859-1");
  EXPECT_EQ(fallback.cues.front().runs.front().text, "\xc3\xa9");

  options.encoding_override = "windows-1252";
  const SrtParseResult overridden = ParseSubRip(input, options);
  EXPECT_EQ(overridden.detected_encoding, "windows-1252");
  EXPECT_EQ(overridden.encoding_confidence, 1.0);
}

TEST(SrtParserTest, FeedsIncrementallyAndRejectsUnsupportedEncoding) {
  SrtParser parser;
  EXPECT_TRUE(parser.Feed("1\n00:00:00,000 --> "));
  EXPECT_TRUE(parser.Feed("00:00:01,000\ncaption"));
  EXPECT_EQ(parser.Finish().cues.size(), 1U);

  SrtParseOptions options;
  options.encoding_override = "shift_jis";
  EXPECT_TRUE(ParseSubRip("text", options).cues.empty());
}

TEST(SrtParserTest, TextTrackOverrideReDecodesRetainedSubRipBytes) {
  const std::string input = "1\n00:00:00,000 --> 00:00:01,000\n\x80";
  TextTrack track(TextTrackKind::kSubtitles, "", "", 4096);
  ASSERT_TRUE(track.LoadSubRip(input, "windows-1252", true));
  ASSERT_EQ(track.cues().size(), 1U);
  EXPECT_EQ(track.TextEncoding(), "windows-1252");
  EXPECT_EQ(track.cues().front().runs.front().text, "\xe2\x82\xac");
  ASSERT_TRUE(track.SetEncodingOverride("ISO_8859-1"));
  EXPECT_EQ(track.TextEncoding(), "iso-8859-1");
  EXPECT_EQ(track.cues().front().runs.front().text, "\xc2\x80");
  EXPECT_FALSE(track.SetEncodingOverride("shift_jis"));
  EXPECT_EQ(track.TextEncoding(), "iso-8859-1");
}

}
}
