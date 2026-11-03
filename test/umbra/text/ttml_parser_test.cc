#include "umbra/text/ttml_parser.h"

#include <string_view>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "umbra/player/text_track.h"

namespace umbra::text {
namespace {

TEST(TtmlParserTest, ParsesOffsetTimesAndStyledText) {
  constexpr std::string_view input =
      "<tt xmlns='http://www.w3.org/ns/ttml' "
      "xmlns:tts='http://www.w3.org/ns/ttml#styling'>"
      "<body begin='1s'><div><p begin='500ms' dur='2s'>"
      "Hello <span tts:fontStyle='italic'>world</span>"
      "</p></div></body></tt>";
  const auto parsed = ParseTtml(input);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->text_cues.size(), 1U);
  const player::TextCue& cue = parsed->text_cues.front();
  EXPECT_DOUBLE_EQ(cue.start_time, 1.5);
  EXPECT_DOUBLE_EQ(cue.end_time, 3.5);
  ASSERT_EQ(cue.runs.size(), 2U);
  EXPECT_EQ(cue.runs[0].text, "Hello ");
  EXPECT_EQ(cue.runs[1].text, "world");
  EXPECT_TRUE(cue.runs[1].italic);
}

TEST(TtmlParserTest, AppliesFrameAndTickRates) {
  constexpr std::string_view input =
      "<tt ttp:frameRate='25' ttp:tickRate='100'>"
      "<body><div><p begin='50t' dur='25f'>x</p></div></body></tt>";
  const auto parsed = ParseTtml(input);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->text_cues.size(), 1U);
  EXPECT_DOUBLE_EQ(parsed->text_cues[0].start_time, 0.5);
  EXPECT_DOUBLE_EQ(parsed->text_cues[0].end_time, 1.5);
}

TEST(TtmlParserTest, AppliesFrameRateMultiplierToClockTime) {
  constexpr std::string_view input =
      "<tt ttp:frameRate='30' ttp:frameRateMultiplier='1000 1001'>"
      "<body><div><p begin='00:00:01:15' dur='30f'>x</p></div></body></tt>";
  const auto parsed = ParseTtml(input);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->text_cues.size(), 1U);
  EXPECT_NEAR(parsed->text_cues[0].start_time, 1.5005, 0.000001);
  EXPECT_NEAR(parsed->text_cues[0].end_time, 2.5015, 0.000001);
}

TEST(TtmlParserTest, RejectsExternalEntityPayloads) {
  constexpr std::string_view input =
      "<!DOCTYPE tt [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>"
      "<tt><body><div><p begin='0s' dur='1s'>&x;</p></div></body></tt>";
  const auto parsed = ParseTtml(input);
  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(parsed.status().code(), absl::StatusCode::kDataLoss);
}

TEST(TtmlParserTest, RejectsBillionLaughsPayload) {
  constexpr std::string_view input =
      "<!DOCTYPE tt [<!ENTITY a 'ha'><!ENTITY b '&a;&a;&a;&a;'>]>"
      "<tt><body><div><p begin='0s' dur='1s'>&b;</p></div></body></tt>";
  EXPECT_FALSE(ParseTtml(input).ok());
}

TEST(TtmlParserTest, EnforcesDocumentAndDepthLimits) {
  TtmlParserOptions options;
  options.max_document_bytes = 8;
  EXPECT_FALSE(ParseTtml("<tt><body/></tt>", options).ok());
  options.max_document_bytes = 1024;
  options.max_depth = 2;
  EXPECT_FALSE(ParseTtml("<tt><body><div/></body></tt>", options).ok());
  options.max_depth = 64;
  options.max_cues = 1;
  EXPECT_FALSE(ParseTtml("<tt><body><div><p begin='0s' dur='1s'>a</p>"
                         "<p begin='1s' dur='1s'>b</p></div></body></tt>",
                         options)
                   .ok());
}

TEST(TtmlParserTest, AppliesSampleTimeOffset) {
  constexpr std::string_view input =
      "<tt><body><div><p begin='1s' dur='2s'>x</p></div></body></tt>";
  TtmlParserOptions options;
  options.sample_time_offset = 8.0;
  const auto parsed = ParseTtml(input, options);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->text_cues.size(), 1U);
  EXPECT_DOUBLE_EQ(parsed->text_cues[0].start_time, 9.0);
  EXPECT_DOUBLE_EQ(parsed->text_cues[0].end_time, 11.0);
}

}
}
