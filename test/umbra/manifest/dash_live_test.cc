#include "umbra/manifest/dash_live.h"

#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fake_http_transport.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

using umbra::source::test::FakeHttpTransport;

const absl::Time kLocalNow = absl::UnixEpoch() + absl::Seconds(1'000'010);

std::string ReadFixture(std::string_view name) {
  std::ifstream input(std::string(ALUMBRA_MANIFEST_FIXTURE_DIR) + "/" +
                      std::string(name));
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Presentation TimingPresentation(std::string scheme, std::string value) {
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  presentation.type = PresentationType::kDynamic;
  DashPresentationExtension extension;
  extension.utc_timings.push_back(
      {.scheme_id_uri = std::move(scheme), .value = std::move(value)});
  presentation.dash_extension = std::move(extension);
  return presentation;
}

void RunUntilIdle(base::TestTaskEnvironment& environment) {
  environment.RunUntilIdle();
}

std::optional<absl::Duration> ResolveHttpTiming(
    const std::string& scheme, const std::string& url,
    const std::function<void(const std::shared_ptr<FakeHttpTransport>&)>&
        respond,
    penumbra::HttpMethod expected_method) {
  base::TestTaskEnvironment environment;
  auto transport = std::make_shared<FakeHttpTransport>();
  std::optional<absl::Duration> result;
  ResolveDashUtcOffset(
      TimingPresentation(scheme, url), transport,
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
      [] { return kLocalNow; }, {},
      [&result](absl::StatusOr<absl::Duration> offset) {
        if (offset.ok()) {
          result = *offset;
        }
      });
  RunUntilIdle(environment);
  EXPECT_EQ(transport->Requests().size(), 1);
  if (transport->Requests().empty()) {
    return std::nullopt;
  }
  EXPECT_EQ(transport->Requests()[0]->request.method, expected_method);
  respond(transport);
  RunUntilIdle(environment);
  return result;
}

TEST(DashUtcTimingTest, DirectUsesTheManifestValueAndTriesEntriesInOrder) {
  base::TestTaskEnvironment environment;
  Presentation presentation = TimingPresentation(
      "urn:mpeg:dash:utc:direct:2014", "1970-01-12T13:46:50Z");
  presentation.dash_extension->utc_timings.insert(
      presentation.dash_extension->utc_timings.begin(), {"unsupported", ""});
  std::optional<absl::Duration> result;
  ResolveDashUtcOffset(
      presentation, std::make_shared<FakeHttpTransport>(),
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
      [] { return kLocalNow; }, {},
      [&result](absl::StatusOr<absl::Duration> offset) {
        if (offset.ok()) {
          result = *offset;
        }
      });
  RunUntilIdle(environment);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, absl::ZeroDuration());
}

TEST(DashUtcTimingTest, HttpXsdateReadsTheResponseBody) {
  const auto offset = ResolveHttpTiming(
      "urn:mpeg:dash:utc:http-xsdate:2014", "https://time.example/xsdate",
      [](const std::shared_ptr<FakeHttpTransport>& transport) {
        transport->Respond(0, 200, {});
        const std::string body = "1970-01-12T13:46:51Z";
        transport->Chunk(0,
                         std::vector<std::uint8_t>(body.begin(), body.end()));
        transport->Complete(0, 200, body.size());
      },
      penumbra::HttpMethod::kGet);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, absl::Seconds(1));
}

TEST(DashUtcTimingTest, HttpIsoReadsTheResponseBody) {
  const auto offset = ResolveHttpTiming(
      "urn:mpeg:dash:utc:http-iso:2014", "https://time.example/iso",
      [](const std::shared_ptr<FakeHttpTransport>& transport) {
        transport->Respond(0, 200, {});
        const std::string body = "1970-01-12T13:46:49Z";
        transport->Chunk(0,
                         std::vector<std::uint8_t>(body.begin(), body.end()));
        transport->Complete(0, 200, body.size());
      },
      penumbra::HttpMethod::kGet);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, absl::Seconds(-1));
}

TEST(DashUtcTimingTest, HttpHeadReadsTheDateHeader) {
  const auto offset = ResolveHttpTiming(
      "urn:mpeg:dash:utc:http-head:2014", "https://time.example/head",
      [](const std::shared_ptr<FakeHttpTransport>& transport) {
        transport->Respond(
            0, 200,
            {{.name = "Date", .value = "Mon, 12 Jan 1970 13:46:51 GMT"}});
        transport->Complete(0, 200, 0);
      },
      penumbra::HttpMethod::kHead);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, absl::Seconds(1));
}

TEST(DashUtcTimingTest, HttpNtpReadsTheTransmitTimestamp) {
  const auto offset = ResolveHttpTiming(
      "urn:mpeg:dash:utc:http-ntp:2014", "https://time.example/ntp",
      [](const std::shared_ptr<FakeHttpTransport>& transport) {
        std::vector<std::uint8_t> packet(48);
        const std::uint32_t ntp_seconds = 2'209'988'811;
        packet[40] = static_cast<std::uint8_t>(ntp_seconds >> 24);
        packet[41] = static_cast<std::uint8_t>(ntp_seconds >> 16);
        packet[42] = static_cast<std::uint8_t>(ntp_seconds >> 8);
        packet[43] = static_cast<std::uint8_t>(ntp_seconds);
        transport->Respond(0, 200, {});
        transport->Chunk(0, packet);
        transport->Complete(0, 200, packet.size());
      },
      penumbra::HttpMethod::kGet);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, absl::Seconds(1));
}

TEST(DashUtcTimingTest, FailedServerFallsThroughToTheNextTimingEntry) {
  base::TestTaskEnvironment environment;
  Presentation presentation = TimingPresentation(
      "urn:mpeg:dash:utc:http-xsdate:2014", "https://time.example/fail");
  presentation.dash_extension->utc_timings.push_back(
      {"urn:mpeg:dash:utc:direct:2014", "1970-01-12T13:46:51Z"});
  auto transport = std::make_shared<FakeHttpTransport>();
  std::optional<absl::Duration> result;
  ResolveDashUtcOffset(
      presentation, transport,
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
      [] { return kLocalNow; }, {},
      [&result](absl::StatusOr<absl::Duration> offset) {
        if (offset.ok()) {
          result = *offset;
        }
      });
  RunUntilIdle(environment);
  transport->Fail(0, absl::UnavailableError("timing server unavailable"));
  RunUntilIdle(environment);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, absl::Seconds(1));
}

TEST(DashUtcTimingTest, MissingTimingUsesTheLocalClockOffset) {
  base::TestTaskEnvironment environment;
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  std::optional<absl::Duration> result;
  ResolveDashUtcOffset(
      presentation, std::make_shared<FakeHttpTransport>(),
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
      [] { return kLocalNow; }, {},
      [&result](absl::StatusOr<absl::Duration> offset) {
        if (offset.ok()) {
          result = *offset;
        }
      });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, absl::ZeroDuration());
}

TEST(DashUtcTimingTest, UsesConfiguredHttpXsdateFallbackWhenTimingIsAbsent) {
  base::TestTaskEnvironment environment;
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  auto transport = std::make_shared<FakeHttpTransport>();
  std::optional<absl::Duration> result;
  ResolveDashUtcOffset(
      presentation, transport,
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
      [] { return kLocalNow; }, "https://time.example/fallback",
      [&result](absl::StatusOr<absl::Duration> offset) {
        if (offset.ok()) {
          result = *offset;
        }
      });
  RunUntilIdle(environment);
  ASSERT_EQ(transport->Requests().size(), 1U);
  EXPECT_EQ(transport->Requests().front()->request.url,
            "https://time.example/fallback");
  transport->Respond(0, 200, {});
  const std::string body = "1970-01-12T13:46:51Z";
  transport->Chunk(0, std::vector<std::uint8_t>(body.begin(), body.end()));
  transport->Complete(0, 200, body.size());
  RunUntilIdle(environment);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, absl::Seconds(1));
}

Presentation LivePresentation() {
  Presentation presentation;
  presentation.protocol = Protocol::kDash;
  presentation.type = PresentationType::kDynamic;
  LiveInfo live;
  live.availability_start_time = kLocalNow - absl::Seconds(10);
  live.window_start = penumbra::MediaTime::Zero();
  live.window_end = penumbra::MediaTime::FromMicroseconds(10'000'000);
  live.time_shift_depth = penumbra::TimeDelta::FromMicroseconds(6'000'000);
  live.suggested_presentation_delay =
      penumbra::TimeDelta::FromMicroseconds(2'000'000);
  live.target_segment_duration =
      penumbra::TimeDelta::FromMicroseconds(2'000'000);
  presentation.live = live;
  std::vector<Segment> segments{
      {.sequence_number = 1,
       .start = penumbra::MediaTime::Zero(),
       .duration = penumbra::TimeDelta::FromMicroseconds(2'000'000),
       .uri = "s1.m4s",
       .byte_range = std::nullopt,
       .init_index = std::nullopt,
       .key_index = std::nullopt,
       .discontinuity_sequence = 0,
       .gap = false,
       .bitrate_kbps = std::nullopt,
       .program_date_time = std::nullopt,
       .hls_parts = {}},
      {.sequence_number = 2,
       .start = penumbra::MediaTime::FromMicroseconds(2'000'000),
       .duration = penumbra::TimeDelta::FromMicroseconds(2'000'000),
       .uri = "s2.m4s",
       .byte_range = std::nullopt,
       .init_index = std::nullopt,
       .key_index = std::nullopt,
       .discontinuity_sequence = 0,
       .gap = false,
       .bitrate_kbps = std::nullopt,
       .program_date_time = std::nullopt,
       .hls_parts = {}},
      {.sequence_number = 3,
       .start = penumbra::MediaTime::FromMicroseconds(4'000'000),
       .duration = penumbra::TimeDelta::FromMicroseconds(2'000'000),
       .uri = "s3.m4s",
       .byte_range = std::nullopt,
       .init_index = std::nullopt,
       .key_index = std::nullopt,
       .discontinuity_sequence = 0,
       .gap = false,
       .bitrate_kbps = std::nullopt,
       .program_date_time = std::nullopt,
       .hls_parts = {}},
      {.sequence_number = 4,
       .start = penumbra::MediaTime::FromMicroseconds(6'000'000),
       .duration = penumbra::TimeDelta::FromMicroseconds(2'000'000),
       .uri = "s4.m4s",
       .byte_range = std::nullopt,
       .init_index = std::nullopt,
       .key_index = std::nullopt,
       .discontinuity_sequence = 0,
       .gap = false,
       .bitrate_kbps = std::nullopt,
       .program_date_time = std::nullopt,
       .hls_parts = {}},
      {.sequence_number = 5,
       .start = penumbra::MediaTime::FromMicroseconds(8'000'000),
       .duration = penumbra::TimeDelta::FromMicroseconds(2'000'000),
       .uri = "s5.m4s",
       .byte_range = std::nullopt,
       .init_index = std::nullopt,
       .key_index = std::nullopt,
       .discontinuity_sequence = 0,
       .gap = false,
       .bitrate_kbps = std::nullopt,
       .program_date_time = std::nullopt,
       .hls_parts = {}}};
  auto index = ExplicitSegmentIndex::Create({.segments = std::move(segments),
                                             .first_sequence_number = 1,
                                             .init_sections = {},
                                             .key_references = {}});
  auto track = std::make_shared<Track>();
  track->id = TrackId("track");
  track->bandwidth_bps = 1'000;
  track->format = VideoTrackFormat{};
  track->segments = SegmentIndexProvider::Resolved(*index);
  auto set = std::make_shared<SwitchingSet>();
  set->id = SwitchingSetId("set");
  set->type = penumbra::StreamType::kVideo;
  set->tracks.push_back(track);
  auto period = std::make_shared<Period>();
  period->id = PeriodId("period");
  period->start = penumbra::MediaTime::Zero();
  period->duration = penumbra::TimeDelta::Unknown();
  period->switching_sets.push_back(set);
  presentation.periods.push_back(period);
  return presentation;
}

TEST(DashAvailabilityTest, AppliesUtcOffsetAndBoundsTheTimeShiftWindow) {
  const auto window = DashAvailableWindow(
      LivePresentation(), kLocalNow - absl::Seconds(30), absl::Seconds(30));
  ASSERT_TRUE(window.ok());
  EXPECT_EQ(window->available_end,
            penumbra::MediaTime::FromMicroseconds(10'000'000));
  EXPECT_EQ(window->start, penumbra::MediaTime::FromMicroseconds(4'000'000));
}

TEST(DashAvailabilityTest, CorrectsADeviceClockThatIsThirtySecondsFast) {
  const auto window = DashAvailableWindow(
      LivePresentation(), kLocalNow + absl::Seconds(30), absl::Seconds(-30));
  ASSERT_TRUE(window.ok());
  EXPECT_EQ(window->available_end,
            penumbra::MediaTime::FromMicroseconds(10'000'000));
  EXPECT_EQ(window->start, penumbra::MediaTime::FromMicroseconds(4'000'000));
}

TEST(DashAvailabilityTest, StartUsesConfiguredDelayThenManifestDelay) {
  const Presentation presentation = LivePresentation();
  auto configured = DashLiveStartPosition(presentation, kLocalNow, {}, 1'000);
  ASSERT_TRUE(configured.ok());
  EXPECT_EQ(*configured, penumbra::MediaTime::FromMicroseconds(9'000'000));
  auto suggested = DashLiveStartPosition(presentation, kLocalNow);
  ASSERT_TRUE(suggested.ok());
  EXPECT_EQ(*suggested, penumbra::MediaTime::FromMicroseconds(8'000'000));
}

TEST(DashAvailabilityTest, AvailabilityTimeOffsetMakesSegmentsAvailableEarly) {
  Presentation presentation = LivePresentation();
  auto period = std::make_shared<Period>(*presentation.periods.front());
  auto set = std::make_shared<SwitchingSet>(*period->switching_sets.front());
  auto track = std::make_shared<Track>(*set->tracks.front());
  track->extension =
      DashTrackExtension{.quality_ranking = std::nullopt,
                         .mime_type = std::nullopt,
                         .availability_time_offset =
                             penumbra::TimeDelta::FromMicroseconds(2'000'000),
                         .availability_time_offset_infinite = false,
                         .dependency_ids = {},
                         .base_urls = {},
                         .supplemental_properties = {}};
  set->tracks.front() = track;
  period->switching_sets.front() = set;
  presentation.periods.front() = period;
  const auto window =
      DashAvailableWindow(presentation, kLocalNow - absl::Seconds(2));
  ASSERT_TRUE(window.ok());
  EXPECT_EQ(window->available_end,
            penumbra::MediaTime::FromMicroseconds(10'000'000));
}

TEST(DashAvailabilityTest, DynamicTemplateIsBoundedByPublishTimeAndOffset) {
  const auto parsed = ParseDashMpd(ReadFixture("dynamic-template.mpd"));
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const Track& track =
      *parsed->periods.front()->switching_sets.front()->tracks.front();
  ASSERT_NE(track.segments.Index(), nullptr);
  EXPECT_EQ(track.segments.Index()->size(), 6);
  const auto* extension = std::get_if<DashTrackExtension>(&track.extension);
  ASSERT_NE(extension, nullptr);
  ASSERT_TRUE(extension->availability_time_offset.has_value());
  EXPECT_EQ(extension->availability_time_offset->InMicroseconds(), 2'000'000);
  EXPECT_TRUE(parsed->periods.front()->duration.is_unknown());
}

TEST(DashRefreshPlanTest, UsesMinimumUpdatePeriodAndLocation) {
  Presentation presentation = LivePresentation();
  presentation.live->minimum_update_period =
      penumbra::TimeDelta::FromMicroseconds(2'000'000);
  DashPresentationExtension extension;
  extension.locations.emplace_back("../next/live.mpd");
  presentation.dash_extension = extension;
  const DashRefreshPlan plan =
      PlanDashRefresh(presentation, "https://example.test/a/live.mpd");
  EXPECT_TRUE(plan.refresh);
  EXPECT_FALSE(plan.end_playback);
  EXPECT_EQ(plan.delay, absl::Seconds(2));
  EXPECT_EQ(plan.manifest_url, "https://example.test/next/live.mpd");
}

TEST(DashRefreshPlanTest, StaticTransitionEndsPlayback) {
  Presentation presentation = LivePresentation();
  presentation.type = PresentationType::kStatic;
  presentation.live.reset();
  const DashRefreshPlan plan =
      PlanDashRefresh(presentation, "https://example.test/live.mpd");
  EXPECT_FALSE(plan.refresh);
  EXPECT_TRUE(plan.end_playback);
}

TEST(DashMpdRefresherTest, RefreshesAtMinimumUpdatePeriodAndEndsOnStaticMpd) {
  auto initial = ParseDashMpd(ReadFixture("dynamic-template.mpd"));
  ASSERT_TRUE(initial.ok()) << initial.status();
  base::TestTaskEnvironment environment;
  auto transport = std::make_shared<FakeHttpTransport>();
  std::optional<DashMpdRefreshUpdate> update;
  std::optional<absl::Status> refresh_error;
  auto refresher = DashMpdRefresher::Create(
      {.presentation =
           std::make_shared<const Presentation>(std::move(*initial)),
       .manifest_url = "https://origin.test/root.mpd",
       .transport = transport,
       .runner =
           environment.CreateSequencedTaskRunner(base::TaskPriority::kControl),
       .on_update = [&update, &refresh_error](
                        absl::StatusOr<DashMpdRefreshUpdate> result) {
         if (result.ok()) {
           update = std::move(*result);
         } else {
           refresh_error = result.status();
         }
       }});
  ASSERT_TRUE(refresher.ok()) << refresher.status();
  (*refresher)->Start();
  environment.RunUntilIdle();
  environment.FastForwardBy(absl::Seconds(2));
  ASSERT_EQ(transport->Requests().size(), 1U);
  EXPECT_EQ(transport->Requests().front()->request.url,
            "https://edge.test/live.mpd");
  transport->Respond(0, 200, {});
  const std::string static_xml = ReadFixture("static-transition.mpd");
  transport->Chunk(
      0, std::vector<std::uint8_t>(static_xml.begin(), static_xml.end()));
  transport->Complete(0, 200, static_xml.size());
  environment.RunUntilIdle();
  ASSERT_TRUE(update.has_value())
      << (refresh_error.has_value() ? refresh_error->ToString() : "no update");
  EXPECT_TRUE(update->ended);
  EXPECT_EQ(update->presentation->type, PresentationType::kStatic);
  (*refresher)->Stop();
}

}
}
