#include "umbra/manifest/ids.h"

#include <set>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "umbra/manifest/url_template.h"

namespace umbra::manifest {
namespace {

TEST(IdsTest, DashIdsDeriveFromProtocolKeys) {
  EXPECT_EQ(DashTrackId("1", "video-720").Value(), "dash/1/video-720");
  EXPECT_EQ(DashSwitchingSetId("1").Value(), "dash/1");
  EXPECT_EQ(DashPeriodId("main").Value(), "dash/p/main");
  EXPECT_EQ(DashPeriodIdByIndex(3).Value(), "dash/i/3");
}

TEST(IdsTest, KeyPartsEscapeTheSeparatorAndControlBytes) {
  EXPECT_EQ(EncodeKeyPart("a/b"), "a%2Fb");
  EXPECT_EQ(EncodeKeyPart("100%"), "100%25");
  EXPECT_EQ(EncodeKeyPart("caf\xC3\xA9"), "caf%C3%A9");
  EXPECT_NE(DashTrackId("a/b", "c"), DashTrackId("a", "b/c"));
}

TEST(IdsTest, HlsRenditionIdsIgnoreTheGroupInTheSetAndKeepItInTheTrack) {
  HlsRenditionKey low;
  low.group_id = "aac-64k";
  low.name = "English";
  low.language = "en";
  HlsRenditionKey high = low;
  high.group_id = "aac-128k";
  EXPECT_EQ(HlsRenditionSwitchingSetId(low), HlsRenditionSwitchingSetId(high));
  EXPECT_NE(HlsRenditionTrackId(low), HlsRenditionTrackId(high));
}

TEST(IdsTest, HlsRenditionIdsSeparateTypesAndCharacteristics) {
  HlsRenditionKey audio;
  audio.name = "English";
  audio.language = "en";
  HlsRenditionKey subtitles = audio;
  subtitles.type = RenditionType::kSubtitles;
  HlsRenditionKey described = audio;
  described.characteristics = "public.accessibility.describes-video";
  EXPECT_NE(HlsRenditionSwitchingSetId(audio),
            HlsRenditionSwitchingSetId(subtitles));
  EXPECT_NE(HlsRenditionSwitchingSetId(audio),
            HlsRenditionSwitchingSetId(described));
}

TEST(IdsTest, HlsVariantIdsDoNotDependOnAUri) {
  HlsVariantKey key;
  key.bandwidth = 2'500'000;
  key.width = 1280;
  key.height = 720;
  key.frame_rate = "29.970";
  key.video_range = "SDR";
  key.codecs = "avc1.64001f,mp4a.40.2";
  const TrackId first = HlsVariantTrackId(key);
  EXPECT_EQ(first, HlsVariantTrackId(key));
  key.ordinal = 1;
  EXPECT_NE(first, HlsVariantTrackId(key));
  EXPECT_NE(HlsVariantTrackId(key), HlsTrickPlayTrackId(key));
}

TEST(IdsTest, HlsPeriodIdsFollowTheDiscontinuitySequence) {
  EXPECT_EQ(HlsPeriodId(7).Value(), "hls/d/7");
  EXPECT_LT(HlsPeriodId(1), HlsPeriodId(2));
}

TEST(IdsTest, DistinctInputsGiveDistinctIds) {
  std::set<std::string> seen;
  for (const char* adaptation : {"1", "2", "1/2", "1%2F2"}) {
    for (const char* representation : {"a", "b", "a/b", ""}) {
      EXPECT_TRUE(
          seen.insert(DashTrackId(adaptation, representation).Value()).second);
    }
  }
}

TEST(UrlTemplateTest, ExpandsNumberTimeBandwidthAndRepresentation) {
  const absl::StatusOr<UrlTemplate> parsed = UrlTemplate::Parse(
      "https://cdn.test/$RepresentationID$/$Bandwidth$/"
      "s-$Number%05d$-$Time$.m4s");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  TemplateValues values;
  values.number = 42;
  values.time = 90'000;
  values.bandwidth = 2'000'000;
  values.representation_id = "video";
  EXPECT_EQ(parsed->Expand(values),
            "https://cdn.test/video/2000000/s-00042-90000.m4s");
  EXPECT_TRUE(parsed->UsesNumber());
  EXPECT_TRUE(parsed->UsesTime());
}

TEST(UrlTemplateTest, DoubleDollarIsALiteral) {
  const absl::StatusOr<UrlTemplate> parsed =
      UrlTemplate::Parse("a$$b/$Number$");
  ASSERT_TRUE(parsed.ok());
  TemplateValues values;
  values.number = 1;
  EXPECT_EQ(parsed->Expand(values), "a$b/1");
}

TEST(UrlTemplateTest, NegativeTimesKeepTheirSignBeforeThePadding) {
  const absl::StatusOr<UrlTemplate> parsed = UrlTemplate::Parse("$Time%05d$");
  ASSERT_TRUE(parsed.ok());
  TemplateValues values;
  values.time = -42;
  EXPECT_EQ(parsed->Expand(values), "-00042");
}

TEST(UrlTemplateTest, RejectsMalformedTemplates) {
  for (const char* text :
       {"seg.m4s", "seg-$Number.m4s", "seg-$Unknown$-$Number$",
        "seg-$Number%5d$", "seg-$Number%0d$", "seg-$Number%099d$",
        "seg-$RepresentationID%05d$-$Number$", "seg-$Number%0-3d$", ""}) {
    const absl::StatusOr<UrlTemplate> parsed = UrlTemplate::Parse(text);
    EXPECT_FALSE(parsed.ok()) << text;
    if (!parsed.ok()) {
      EXPECT_EQ(parsed.status().code(), absl::StatusCode::kInvalidArgument);
      EXPECT_NE(parsed.status().message().find("ALUMBRA-2001"),
                std::string::npos);
    }
  }
}

}
}
