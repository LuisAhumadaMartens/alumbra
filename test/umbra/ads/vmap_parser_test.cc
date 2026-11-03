#include "umbra/ads/vmap_parser.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "umbra/ads/ad_manager.h"

namespace umbra::ads {
namespace {

constexpr std::string_view kVmap = R"xml(
<VMAP version="1.0">
  <AdBreak timeOffset="start" breakType="linear" breakId="pre">
    <AdSource allowMultipleAds="true" followRedirects="false">
      <AdTagURI templateType="vast3">https://ads.test/pre</AdTagURI>
    </AdSource>
    <TrackingEvents>
      <Tracking event="breakStart">https://track.test/start</Tracking>
      <Tracking event="breakEnd">https://track.test/end</Tracking>
      <Tracking event="error">https://track.test/error</Tracking>
    </TrackingEvents>
  </AdBreak>
  <AdBreak timeOffset="00:00:30.500" breakType="linear" breakId="clock">
    <AdSource><AdTagURI templateType="vast4">https://ads.test/clock</AdTagURI></AdSource>
  </AdBreak>
  <AdBreak timeOffset="25%" breakType="linear" breakId="percent">
    <AdSource><AdTagURI>https://ads.test/percent</AdTagURI></AdSource>
  </AdBreak>
  <AdBreak timeOffset="end" breakType="linear" breakId="post">
    <AdSource><AdTagURI>https://ads.test/post</AdTagURI></AdSource>
  </AdBreak>
  <AdBreak timeOffset="00:00:10.000" breakType="nonlinear" breakId="ignored">
    <AdSource><AdTagURI>https://ads.test/ignored</AdTagURI></AdSource>
  </AdBreak>
  <AdBreak timeOffset="broken" breakType="linear" breakId="invalid">
    <AdSource><AdTagURI>https://ads.test/invalid</AdTagURI></AdSource>
  </AdBreak>
</VMAP>)xml";

TEST(VmapParserTest, ResolvesEveryOffsetAndSkipsInvalidOrNonlinearBreaks) {
  std::vector<VmapParseError> errors;
  auto result = ParseVmap(
      kVmap, penumbra::TimeDelta::FromSeconds(120), {},
      [&errors](const VmapParseError& error) { errors.push_back(error); });
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 4U);
  EXPECT_EQ(result->breaks[0].id, "pre");
  EXPECT_EQ(result->breaks[0].offset.value, 0.0);
  EXPECT_EQ(result->breaks[1].id, "clock");
  EXPECT_EQ(result->breaks[1].offset.value, 30.5);
  EXPECT_EQ(result->breaks[2].id, "percent");
  EXPECT_EQ(result->breaks[2].offset.value, 30.0);
  EXPECT_EQ(result->breaks[3].id, "post");
  EXPECT_EQ(result->breaks[3].offset.value, 120.0);
  ASSERT_EQ(result->errors.size(), 1U);
  EXPECT_EQ(result->errors[0].break_id, "invalid");
  ASSERT_EQ(errors.size(), 1U);
  EXPECT_EQ(errors[0].break_id, "invalid");
  EXPECT_EQ(result->breaks[0].sources[0].kind, AdSourceKind::kAdTagUri);
  EXPECT_EQ(result->breaks[0].sources[0].template_type, "vast3");
  EXPECT_TRUE(result->breaks[0].sources[0].allow_multiple_ads);
  EXPECT_FALSE(result->breaks[0].sources[0].follow_redirects);
  ASSERT_EQ(result->breaks[0].tracking_events.size(), 3U);
}

TEST(VmapParserTest, ResolvesPositionOffsetsWhenTheyAreTheOnlyOffsetForm) {
  constexpr std::string_view xml = R"xml(
<VMAP version="1.0">
<AdBreak timeOffset="#1" breakType="linear" breakId="one"><AdSource><AdTagURI>https://ads.test/1</AdTagURI></AdSource></AdBreak>
<AdBreak timeOffset="#2" breakType="linear" breakId="two"><AdSource><AdTagURI>https://ads.test/2</AdTagURI></AdSource></AdBreak>
</VMAP>)xml";
  auto result = ParseVmap(xml, penumbra::TimeDelta::FromSeconds(120));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 2U);
  EXPECT_EQ(result->breaks[0].offset.value, 40.0);
  EXPECT_EQ(result->breaks[1].offset.value, 80.0);
}

TEST(VmapParserTest, ReportsPositionOffsetsMixedWithTimedOffsets) {
  constexpr std::string_view xml = R"xml(
<VMAP version="1.0">
<AdBreak timeOffset="#1" breakType="linear" breakId="position"><AdSource><AdTagURI>https://ads.test/1</AdTagURI></AdSource></AdBreak>
<AdBreak timeOffset="00:00:20.000" breakType="linear" breakId="timed"><AdSource><AdTagURI>https://ads.test/2</AdTagURI></AdSource></AdBreak>
</VMAP>)xml";
  auto result = ParseVmap(xml, penumbra::TimeDelta::FromSeconds(120));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 1U);
  EXPECT_EQ(result->breaks[0].id, "timed");
  ASSERT_EQ(result->errors.size(), 1U);
  EXPECT_EQ(result->errors[0].break_id, "position");
}

TEST(VmapParserTest, ImaHtml5PreMidPostSampleCuePointsMatchReferenceTimeline) {
  constexpr std::string_view sample = R"xml(
<vmap:VMAP xmlns:vmap="http://www.iab.net/videosuite/vmap" version="1.0">
<vmap:AdBreak timeOffset="start" breakType="linear" breakId="preroll"><vmap:AdSource><vmap:AdTagURI templateType="vast3">https://ads.test/pre</vmap:AdTagURI></vmap:AdSource></vmap:AdBreak>
<vmap:AdBreak timeOffset="00:00:15.000" breakType="linear" breakId="midroll-1"><vmap:AdSource><vmap:AdTagURI templateType="vast3">https://ads.test/mid</vmap:AdTagURI></vmap:AdSource></vmap:AdBreak>
<vmap:AdBreak timeOffset="end" breakType="linear" breakId="postroll"><vmap:AdSource><vmap:AdTagURI templateType="vast3">https://ads.test/post</vmap:AdTagURI></vmap:AdSource></vmap:AdBreak>
</vmap:VMAP>)xml";
  auto result = ParseVmap(sample, penumbra::TimeDelta::FromSeconds(100));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 3U);
  EXPECT_EQ(result->breaks[0].offset.value, 0.0);
  EXPECT_EQ(result->breaks[1].offset.value, 15.0);
  EXPECT_EQ(result->breaks[2].offset.value, 100.0);
}

TEST(VmapParserTest, ConformanceFixtureMatchesScheduleAndBreakBeacons) {
  std::ifstream input(std::string(ALUMBRA_VAST_FIXTURE_DIR) +
                      "/conformance/vmap_schedule.xml");
  const std::string xml{std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>()};
  auto result = ParseVmap(xml, penumbra::TimeDelta::FromSeconds(100));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 3U);
  EXPECT_EQ(result->breaks[0].id, "pre");
  EXPECT_EQ(result->breaks[0].offset.value, 0.0);
  EXPECT_EQ(result->breaks[1].id, "mid");
  EXPECT_EQ(result->breaks[1].offset.value, 15.0);
  EXPECT_EQ(result->breaks[2].id, "post");
  EXPECT_EQ(result->breaks[2].offset.value, 100.0);
  ASSERT_EQ(result->breaks[0].tracking_events.size(), 2U);
  EXPECT_EQ(result->breaks[0].tracking_events[0].uri,
            "https://track.example.test/pre-start");
  EXPECT_EQ(result->breaks[0].tracking_events[1].uri,
            "https://track.example.test/pre-end");
  EXPECT_EQ(result->breaks[1].sources[0].uri, "https://ads.example.test/mid");
}

TEST(VmapParserTest, ExpandsRepeatAfterAndRejectsMalformedDocument) {
  constexpr std::string_view xml = R"xml(
<VMAP version="1.0"><AdBreak timeOffset="00:00:10.000" repeatAfter="00:00:20.000" breakType="linear" breakId="repeat">
<AdSource><AdTagURI>https://ads.test</AdTagURI></AdSource>
</AdBreak></VMAP>)xml";
  auto result = ParseVmap(xml, penumbra::TimeDelta::FromSeconds(70));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->breaks.size(), 4U);
  EXPECT_EQ(result->breaks[0].offset.value, 10.0);
  EXPECT_EQ(result->breaks[1].offset.value, 30.0);
  EXPECT_EQ(result->breaks[2].offset.value, 50.0);
  EXPECT_EQ(result->breaks[3].offset.value, 70.0);
  EXPECT_FALSE(
      ParseVmap("<VMAP><AdBreak>", penumbra::TimeDelta::FromSeconds(10)).ok());
}

TEST(VmapParserTest, ParsesInlineVastAndUsesSingleVastAsPreroll) {
  constexpr std::string_view inline_vmap = R"xml(
<VMAP version="1.0"><AdBreak timeOffset="start" breakType="linear" breakId="inline">
<AdSource><VASTAdData><VAST version="4.2"><Ad id="ad-one"><InLine>
<Creatives><Creative><Linear><MediaFiles/></Linear></Creative></Creatives>
</InLine></Ad></VAST></VASTAdData></AdSource></AdBreak></VMAP>)xml";
  auto embedded = ParseVmap(inline_vmap, penumbra::TimeDelta::FromSeconds(10));
  ASSERT_TRUE(embedded.ok()) << embedded.status();
  ASSERT_EQ(embedded->breaks.size(), 1U);
  ASSERT_EQ(embedded->breaks[0].pod.ads.size(), 1U);
  EXPECT_EQ(embedded->breaks[0].pod.ads[0].id, "ad-one");

  constexpr std::string_view vast = R"xml(
<VAST version="4.2"><Ad id="single"><InLine><Creatives>
<Creative><Linear><MediaFiles/></Linear></Creative></Creatives></InLine></Ad></VAST>)xml";
  auto fallback = ParseAdSchedule(vast, penumbra::TimeDelta::FromSeconds(10));
  ASSERT_TRUE(fallback.ok()) << fallback.status();
  ASSERT_EQ(fallback->breaks.size(), 1U);
  EXPECT_EQ(fallback->breaks[0].offset.kind, OffsetKind::kStart);
  ASSERT_EQ(fallback->breaks[0].pod.ads.size(), 1U);
}

}
}
