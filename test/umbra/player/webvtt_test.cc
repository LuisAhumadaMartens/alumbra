#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/player/text_track.h"
#include "umbra/player/webvtt_parser.h"

namespace umbra::player {
namespace {

std::string Box(std::string_view type, std::string_view body) {
  const auto size = static_cast<uint32_t>(body.size() + 8);
  std::string box(4, '\0');
  box[0] = static_cast<char>(size >> 24);
  box[1] = static_cast<char>(size >> 16);
  box[2] = static_cast<char>(size >> 8);
  box[3] = static_cast<char>(size);
  box.append(type);
  box.append(body);
  return box;
}

TEST(WebVttTest, ParsesCueAndSettings) {
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00.000,MPEGTS:0\n\n"
      "chapter\n00:01.000 --> 00:02.500 position:25% size:80% line:4 "
      "align:start\n"
      "<b>Hello &amp; goodbye</b>\n");
  ASSERT_EQ(result.cues.size(), 1);
  EXPECT_EQ(result.cues[0].start_time, 1.0);
  EXPECT_EQ(result.cues[0].end_time, 2.5);
  EXPECT_EQ(result.cues[0].position, 25.0);
  EXPECT_EQ(result.cues[0].size, 80.0);
  EXPECT_EQ(result.header_metadata.size(), 1);
  ASSERT_EQ(result.cues[0].runs.size(), 1);
  EXPECT_EQ(result.cues[0].runs[0].text, "Hello & goodbye");
  EXPECT_TRUE(result.cues[0].runs[0].bold);
}

TEST(WebVttTest, ArbitrarySplitsMatchSingleBuffer) {
  const std::string input = "WEBVTT\n\n00:00.000 --> 00:01.000\nA cue\n";
  const WebVttResult expected = WebVttParser::Parse(input);
  for (size_t split = 0; split <= input.size(); ++split) {
    WebVttParser parser;
    std::vector<TextCue> cues;
    const auto callback = [&cues](TextCue cue) {
      cues.push_back(std::move(cue));
    };
    parser.Feed(std::string_view(input).substr(0, split), callback);
    parser.Feed(std::string_view(input).substr(split), callback);
    const WebVttResult actual = parser.Finish();
    ASSERT_EQ(cues.size(), expected.cues.size());
    EXPECT_EQ(cues[0].runs[0].text, expected.cues[0].runs[0].text);
    EXPECT_EQ(cues[0].start_time, expected.cues[0].start_time);
  }
}

TEST(WebVttTest, MalformedCuesProduceWarnings) {
  const WebVttResult result = WebVttParser::Parse(
      "WEBVTT\n\n00:00.000 --> bad\ninvalid\n\n00:01.000 --> 00:02.000\nok\n");
  EXPECT_EQ(result.cues.size(), 1);
  EXPECT_FALSE(result.warnings.empty());
}

TEST(WebVttTest, DecodesWvttCueSample) {
  std::string children = Box("iden", "sample-id");
  children += Box("sttg", "position:20%");
  children += Box("payl", "caption");
  const std::string sample = Box("vttc", children);
  const WebVttResult result = WebVttParser::ParseWvttSample(sample, 1.0, 2.0);
  ASSERT_EQ(result.cues.size(), 1);
  EXPECT_EQ(result.cues[0].start_time, 1.0);
  EXPECT_EQ(result.cues[0].end_time, 2.0);
  EXPECT_EQ(result.cues[0].position, 20.0);
  ASSERT_EQ(result.cues[0].runs.size(), 1);
  EXPECT_EQ(result.cues[0].runs[0].text, "caption");
}

TEST(WebVttTest, EmptyWvttSampleProducesNoCue) {
  const std::string sample = Box("vtte", "");
  const WebVttResult result = WebVttParser::ParseWvttSample(sample, 1.0, 2.0);
  EXPECT_TRUE(result.cues.empty());
  EXPECT_TRUE(result.warnings.empty());
}

}
}
