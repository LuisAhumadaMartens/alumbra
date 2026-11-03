#include "umbra/manifest/dash_mpd_parser.h"

#include <absl/time/time.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "gtest/gtest.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

std::string ReadFixture(std::string_view name) {
  std::ifstream file(std::string(ALUMBRA_MANIFEST_FIXTURE_DIR) + "/" +
                     std::string(name));
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

std::string TimeText(penumbra::TimeDelta duration) {
  return duration.is_finite() ? std::to_string(duration.InMicroseconds())
                              : "unknown";
}

std::string StreamTypeText(penumbra::StreamType type) {
  if (type == penumbra::StreamType::kAudio) {
    return "audio";
  }
  if (type == penumbra::StreamType::kText) {
    return "text";
  }
  return "video";
}

void AppendExplicitSegments(std::ostringstream& summary,
                            const ExplicitSegmentIndex& index);
void AppendDeferredIndex(std::ostringstream& summary,
                         const IndexLocation& location);
void AppendTrackBases(std::ostringstream& summary, const Track& track);
void AppendInitSections(std::ostringstream& summary, const SegmentIndex* index);

void AppendProtectionSummary(std::ostringstream& summary,
                             const ProtectionInfo& protection) {
  summary << "protection="
          << (protection.scheme == penumbra::EncryptionScheme::kCbcs ? "cbcs"
                                                                     : "cenc")
          << " systems=" << protection.system_ids.size()
          << " init_data=" << protection.init_data.size() << " key=";
  if (protection.default_key_id.has_value()) {
    constexpr std::string_view digits = "0123456789abcdef";
    for (const std::uint8_t byte : *protection.default_key_id) {
      summary << digits.at(byte >> 4) << digits.at(byte & 15);
    }
  }
  summary << '\n';
}

void AppendIndexSummary(std::ostringstream& summary, const Track& track) {
  const SegmentIndex* index = track.segments.Index();
  summary << "track bandwidth=" << track.bandwidth_bps.value_or(0)
          << " segments=" << (index == nullptr ? 0 : index->size()) << '\n';
  if (const auto* template_index =
          dynamic_cast<const TemplateSegmentIndex*>(index)) {
    summary << "runs=";
    bool first = true;
    for (const TimelineRun& run : template_index->Runs()) {
      if (!first) {
        summary << ',';
      }
      summary << run.start_ticks << ':' << run.duration_ticks << ':'
              << run.count;
      first = false;
    }
    summary << '\n';
  } else if (const auto* explicit_index =
                 dynamic_cast<const ExplicitSegmentIndex*>(index)) {
    AppendExplicitSegments(summary, *explicit_index);
  } else if (track.segments.Location().has_value()) {
    AppendDeferredIndex(summary, *track.segments.Location());
  }
  AppendTrackBases(summary, track);
  AppendInitSections(summary, index);
}

void AppendExplicitSegments(std::ostringstream& summary,
                            const ExplicitSegmentIndex& index) {
  for (const Segment& segment : index.Segments()) {
    summary << "segment=" << segment.sequence_number << ':' << segment.uri;
    if (segment.byte_range.has_value()) {
      summary << ':' << segment.byte_range->offset << ':'
              << segment.byte_range->length;
    }
    summary << '\n';
  }
}

void AppendDeferredIndex(std::ostringstream& summary,
                         const IndexLocation& location) {
  summary << "deferred="
          << (location.source == IndexLocation::Source::kDashSegmentIndexBox
                  ? "dash-sidx"
                  : "dash-base");
  if (location.byte_range.has_value()) {
    summary << ':' << location.byte_range->offset << ':'
            << location.byte_range->length;
  }
  summary << '\n';
}

void AppendTrackBases(std::ostringstream& summary, const Track& track) {
  const auto* extension = std::get_if<DashTrackExtension>(&track.extension);
  if (extension == nullptr) {
    return;
  }
  for (const std::string& base : extension->base_urls) {
    summary << "base=" << base << '\n';
  }
}

void AppendInitSections(std::ostringstream& summary,
                        const SegmentIndex* index) {
  if (index == nullptr) {
    return;
  }
  for (const InitSection& init : index->InitSections()) {
    summary << "init=" << init.uri;
    if (init.byte_range.has_value()) {
      summary << ':' << init.byte_range->offset << ':'
              << init.byte_range->length;
    }
    summary << '\n';
  }
}

void AppendSetSummary(std::ostringstream& summary, const SwitchingSet& set) {
  summary << "set type=" << StreamTypeText(set.type)
          << " language=" << set.language << " roles=" << set.roles.size()
          << " accessibility=" << set.accessibility.size()
          << " essential=" << set.essential_properties.size()
          << " supplemental=" << set.supplemental_properties.size()
          << " channels=" << set.channels.channel_count << '\n';
  if (set.protection.has_value()) {
    AppendProtectionSummary(summary, *set.protection);
  }
  for (const auto& track : set.tracks) {
    AppendIndexSummary(summary, *track);
  }
}

void AppendPeriodSummary(std::ostringstream& summary, const Period& period) {
  summary << "period=" << period.id.Value()
          << " start_us=" << period.start.InMicroseconds()
          << " duration_us=" << TimeText(period.duration) << '\n';
  for (const auto& set : period.switching_sets) {
    AppendSetSummary(summary, *set);
  }
}

void AppendLiveSummary(std::ostringstream& summary, const LiveInfo& live) {
  summary << "live time_shift_us="
          << (live.time_shift_depth.has_value()
                  ? TimeText(*live.time_shift_depth)
                  : "unset")
          << " delay_us="
          << (live.suggested_presentation_delay.has_value()
                  ? TimeText(*live.suggested_presentation_delay)
                  : "unset")
          << " update_us="
          << (live.minimum_update_period.has_value()
                  ? TimeText(*live.minimum_update_period)
                  : "unset")
          << " target_us="
          << (live.target_segment_duration.has_value()
                  ? TimeText(*live.target_segment_duration)
                  : "unset")
          << " publish="
          << (live.publish_time.has_value() ? "present" : "absent") << '\n';
}

void AppendMetadataSummary(std::ostringstream& summary,
                           const DashPresentationExtension& extension) {
  summary << "metadata profiles=" << extension.profiles.size()
          << " min_buffer_us="
          << (extension.min_buffer_time.has_value()
                  ? TimeText(*extension.min_buffer_time)
                  : "unset")
          << " max_segment_us="
          << (extension.max_segment_duration.has_value()
                  ? TimeText(*extension.max_segment_duration)
                  : "unset")
          << " locations=" << extension.locations.size()
          << " patches=" << extension.patch_locations.size()
          << " utc=" << extension.utc_timings.size() << '\n';
}

std::string GoldenSummary(const Presentation& presentation) {
  std::ostringstream summary;
  summary << "presentation type="
          << (presentation.type == PresentationType::kDynamic ? "dynamic"
                                                              : "static")
          << " duration_us=" << TimeText(presentation.duration) << '\n';
  for (const auto& period : presentation.periods) {
    AppendPeriodSummary(summary, *period);
  }
  if (presentation.live.has_value()) {
    AppendLiveSummary(summary, *presentation.live);
  }
  if (presentation.dash_extension.has_value()) {
    AppendMetadataSummary(summary, *presentation.dash_extension);
  }
  for (const AdBreak& ad_break : presentation.ad_breaks) {
    summary << "ad=" << ad_break.id
            << " start_us=" << ad_break.start.InMicroseconds()
            << " duration_us=" << TimeText(ad_break.duration) << '\n';
  }
  return summary.str();
}

void ExpectGolden(std::string_view fixture) {
  const std::string xml = ReadFixture(std::string(fixture) + ".mpd");
  const std::string golden = ReadFixture(std::string(fixture) + ".golden");
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(GoldenSummary(*parsed), golden);
}

TEST(DashMpdParserTest, ParsesAStaticPresentation) {
  constexpr std::string_view xml =
      "<MPD type='static' mediaPresentationDuration='PT4S'><Period id='p' "
      "duration='PT4S'><AdaptationSet id='a' mimeType='video/mp4' "
      "codecs='avc1.64001f'><Representation id='r' "
      "bandwidth='1000'><SegmentTemplate media='v-$Number$.m4s' duration='2' "
      "timescale='1'/></Representation></AdaptationSet></Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->periods.size(), 1U);
  ASSERT_EQ(parsed->periods[0]->switching_sets.size(), 1U);
  ASSERT_EQ(parsed->periods[0]->switching_sets[0]->tracks.size(), 1U);
  EXPECT_EQ(parsed->periods[0]->switching_sets[0]->tracks[0]->bandwidth_bps,
            1000);
}

TEST(DashMpdParserTest, RejectsExternalEntityDeclarations) {
  EXPECT_FALSE(
      ParseDashMpd("<!DOCTYPE MPD SYSTEM 'https://invalid.test/x'><MPD/>")
          .ok());
  EXPECT_FALSE(
      ParseDashMpd("<!DOCTYPE MPD [<!ENTITY a 'x'><!ENTITY b '&a;&a;'>]><MPD/>")
          .ok());
}

TEST(DashMpdParserTest, EnforcesTheInputByteLimit) {
  EXPECT_FALSE(ParseDashMpd("<MPD/>", 4).ok());
}

TEST(DashMpdParserTest, ParsesFractionalSecondsAndDayComponentsExactly) {
  constexpr std::string_view xml =
      "<MPD type='static' mediaPresentationDuration='P1DT1.25S'><Period id='p' "
      "duration='P1DT1.25S'><AdaptationSet id='a' "
      "mimeType='video/mp4'><Representation id='r' "
      "bandwidth='1000'><SegmentBase/></Representation></AdaptationSet></"
      "Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->duration.InMicroseconds(), 86'401'250'000LL);
}

TEST(DashMpdParserTest, ResolvesCalendarDurationAgainstTheAvailabilityAnchor) {
  constexpr std::string_view xml =
      "<MPD type='static' availabilityStartTime='2024-01-31T00:00:00Z' "
      "mediaPresentationDuration='P1M'><Period id='p' "
      "duration='P1M'><AdaptationSet id='a' "
      "mimeType='video/mp4'><Representation id='r' "
      "bandwidth='1000'><SegmentBase/></Representation></AdaptationSet></"
      "Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->duration.InMicroseconds(), 2'505'600'000'000LL);
  EXPECT_EQ(parsed->periods[0]->duration.InMicroseconds(), 2'505'600'000'000LL);
}

TEST(DashMpdParserTest, UsesTheRemainingPresentationDurationForTheLastPeriod) {
  constexpr std::string_view xml =
      "<MPD type='static' mediaPresentationDuration='PT10S'><Period id='p' "
      "start='PT3S'><AdaptationSet id='a' mimeType='video/mp4'>"
      "<Representation id='r' bandwidth='100'><SegmentBase/></Representation>"
      "</AdaptationSet></Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->periods[0]->duration.InMicroseconds(), 7'000'000);
}

TEST(DashMpdParserTest, ParsesDateTimeFractionsAndOffsetsExactly) {
  constexpr std::string_view xml =
      "<MPD type='dynamic' availabilityStartTime="
      "'2024-03-01T01:00:00.125+01:00' publishTime="
      "'2024-03-01T00:00:00.25Z'><Period id='p'><AdaptationSet id='a' "
      "mimeType='video/mp4'><Representation id='r' bandwidth='100'>"
      "<SegmentBase/></Representation></AdaptationSet></Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_TRUE(parsed->live.has_value());
  ASSERT_TRUE(parsed->live->availability_start_time.has_value());
  ASSERT_TRUE(parsed->live->publish_time.has_value());
  absl::Time expected_availability;
  absl::Time expected_publish;
  std::string error;
  ASSERT_TRUE(absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*S%Ez",
                              "2024-03-01T00:00:00.125Z",
                              &expected_availability, &error));
  ASSERT_TRUE(absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*S%Ez",
                              "2024-03-01T00:00:00.25Z", &expected_publish,
                              &error));
  EXPECT_EQ(*parsed->live->availability_start_time, expected_availability);
  EXPECT_EQ(*parsed->live->publish_time, expected_publish);
}

TEST(DashMpdParserTest, ResolvesAbsoluteRelativeAndHierarchicalBaseUrls) {
  constexpr std::string_view xml =
      "<MPD type='static' "
      "mediaPresentationDuration='PT4S'><BaseURL>https://cdn.example/root/</"
      "BaseURL><Period id='p' "
      "duration='PT4S'><BaseURL>video/</BaseURL><AdaptationSet id='a' "
      "mimeType='video/mp4'><BaseURL>../renditions/</BaseURL><Representation "
      "id='r' bandwidth='1000'><BaseURL>high/</BaseURL><SegmentTemplate "
      "media='$Number$.m4s' "
      "duration='2'/></Representation></AdaptationSet></Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const SegmentIndex* index =
      parsed->periods[0]->switching_sets[0]->tracks[0]->segments.Index();
  ASSERT_NE(index, nullptr);
  EXPECT_EQ(index->At(0).uri, "https://cdn.example/root/renditions/high/1.m4s");
}

TEST(DashMpdParserTest, MatchesTemplateTimelineExpectedModelGolden) {
  ExpectGolden("template-timeline");
}

TEST(DashMpdParserTest, MatchesSegmentListAndSidxExpectedModelGolden) {
  ExpectGolden("list-and-sidx");
}

TEST(DashMpdParserTest, MatchesLiveEventsAndProtectionExpectedModelGolden) {
  ExpectGolden("live-events");
}

TEST(DashMpdParserTest, ExcludesAdaptationWithUnknownEssentialProperty) {
  constexpr std::string_view xml =
      "<MPD type='static' mediaPresentationDuration='PT1S'><Period id='p' "
      "duration='PT1S'><AdaptationSet id='a' mimeType='video/mp4'>"
      "<EssentialProperty schemeIdUri='urn:unknown' value='x'/>"
      "<Representation id='r'><SegmentBase/></Representation>"
      "</AdaptationSet><AdaptationSet id='known' mimeType='audio/mp4'>"
      "<Representation id='a' bandwidth='100'><SegmentBase/></Representation>"
      "</AdaptationSet></Period></MPD>";
  auto parsed = ParseDashMpd(xml);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed->periods[0]->switching_sets.size(), 1U);
  EXPECT_EQ(parsed->periods[0]->switching_sets[0]->id,
            DashSwitchingSetId("known"));
}

}
}
