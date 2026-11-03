#include "umbra/fetch/recovery_ladder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/time/time.h"
#include "fetch_test_support.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "recovery_test_support.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/fetch/content_steering.h"
#include "umbra/fetch/error_classifier.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/segment_fetcher.h"
#include "umbra/manifest/presentation.h"

namespace umbra::fetch::test {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

std::vector<int64_t> Range(int64_t first, int64_t last) {
  std::vector<int64_t> result;
  for (int64_t i = first; i <= last; ++i) {
    result.push_back(i);
  }
  return result;
}

RedundantSource Source(const std::string& pathway,
                       std::shared_ptr<const manifest::Track> track,
                       const std::string& base) {
  RedundantSource source;
  source.pathway_id = pathway;
  source.track = std::move(track);
  source.base_url = base;
  return source;
}

size_t CountUrls(const RoutedTransport& transport, const std::string& needle) {
  return static_cast<size_t>(std::ranges::count_if(
      transport.log, [&needle](const LoggedRequest& entry) {
        return absl::StrContains(entry.url, needle);
      }));
}

base::ErrorDetail DetailOf(const absl::Status& status) {
  auto detail = base::GetErrorDetail(status);
  EXPECT_TRUE(detail.ok());
  EXPECT_TRUE(detail->has_value());
  return detail.ok() && detail->has_value() ? **detail : base::ErrorDetail{};
}

void AddHlsDuplicates(RecoveryFixture& fixture, size_t count = 5) {
  const auto primary = HostTrack("v1", "a", count);
  const auto backup = HostTrack("v1b", "b", count);
  fixture.redundancy->sources["v1"] = {
      Source("a", primary, "https://a.test/manifest"),
      Source("b", backup, "https://b.test/manifest")};
}

TEST(RecoveryLadderTest, CreateRejectsMissingDependencies) {
  const RecoveryDependencies dependencies;
  EXPECT_FALSE(RecoveryLadder::Create(dependencies, RecoveryConfig{}).ok());
}

TEST(RecoveryLadderTest, CleanPlaybackPassesThroughUntouched) {
  RecoveryFixture fixture;
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  EXPECT_THAT(fixture.observer->events, IsEmpty());
  EXPECT_EQ(fixture.sink->ends, 1);
}

TEST(RecoveryLadderTest, RetryCountsAndDelaysMatchSettings) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(20));
  EXPECT_THAT(fixture.transport->TimesMs("init.mp4"),
              ElementsAre(0, 500, 1500, 3500));
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kRetry),
              ElementsAre(500, 1000, 2000));
  ASSERT_THAT(fixture.sink->errors, SizeIs(1));
  const base::ErrorDetail detail = DetailOf(fixture.sink->errors[0]);
  EXPECT_EQ(detail.code, base::ErrorCode::kNetworkRequestFailed);
  EXPECT_EQ(detail.severity, base::ErrorSeverity::kFatal);
  EXPECT_EQ(detail.context.at("http_status"), "503");
  EXPECT_EQ(detail.context.at("attempts"), "3");
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kFatal), 1U);
  const size_t total = fixture.transport->log.size();
  fixture.Advance(absl::Seconds(60));
  EXPECT_EQ(fixture.transport->log.size(), total);
}

TEST(RecoveryLadderTest, CustomRetrySettingsAreHonoured) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(500)).ok());
  RecoveryConfig config;
  config.backoff.max_attempts = 2;
  config.backoff.initial_backoff = absl::Milliseconds(200);
  config.backoff.max_backoff = absl::Milliseconds(300);
  fixture.Create(config);
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(5));
  EXPECT_THAT(fixture.transport->TimesMs("init.mp4"), ElementsAre(0, 200, 500));
  EXPECT_THAT(fixture.sink->errors, SizeIs(1));
}

TEST(RecoveryLadderTest, TransportFailuresAreRetried) {
  RecoveryFixture fixture;
  ASSERT_TRUE(
      fixture.host_a
          ->SetScript(FailureScript(absl::UnavailableError("connection reset")))
          .ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Milliseconds(600));
  EXPECT_THAT(fixture.transport->TimesMs("init.mp4"), ElementsAre(0, 500));
  ASSERT_TRUE(fixture.host_a->SetScript(BodyScript()).ok());
  fixture.Advance(absl::Seconds(2));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
}

TEST(RecoveryLadderTest, SuccessResetsTheAttemptBudget) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Milliseconds(1200));
  int attempts = 0;
  fixture.Run([&] { attempts = fixture.ladder->Attempts(StreamKind::kVideo); });
  EXPECT_EQ(attempts, 2);
  ASSERT_TRUE(fixture.host_a->SetScript(BodyScript()).ok());
  fixture.Advance(absl::Seconds(3));
  fixture.Run([&] { attempts = fixture.ladder->Attempts(StreamKind::kVideo); });
  EXPECT_EQ(attempts, 0);
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4));
}

TEST(RecoveryLadderTest, MidPlayback503ContinuesOnTheRedundantVariant) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 10);
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  fixture.Select(HostTrack("v1", "a", 10));
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1));
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.policy->Set(Seconds(60), Seconds(60));
  fixture.Run([&] { fixture.fetcher->OnBufferChanged(); });
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  EXPECT_THAT(fixture.sink->sequences,
              ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_EQ(fixture.sink->tracks.front(), "v1");
  EXPECT_EQ(fixture.sink->tracks.back(), "v1b");
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kFailover), 1U);
  std::string pathway;
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "b");
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kRenditionExcluded), 0U);
}

TEST(RecoveryLadderTest, MidPlayback503ContinuesOnTheAlternativeBaseUrl) {
  RecoveryFixture fixture;
  const auto track =
      MakeTrack("v1", StreamKind::kVideo,
                MakeIndex(IndexOptions().Base("media").Count(6)));
  fixture.redundancy->sources["v1"] = {
      Source("a", track, "https://a.test/dash/manifest.mpd"),
      Source("b", track, "https://b.test/dash/manifest.mpd")};
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  fixture.Select(track, "https://a.test/dash/manifest.mpd");
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1));
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.policy->Set(Seconds(60), Seconds(60));
  fixture.Run([&] { fixture.fetcher->OnBufferChanged(); });
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4, 5));
  EXPECT_GT(CountUrls(*fixture.transport, "https://b.test/dash/media/seg5"),
            0U);
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/dash/media/seg2"),
            4U);
}

TEST(RecoveryLadderTest, ForbiddenFailsOverWithoutRetryThenIsFatal) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture);
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(403)).ok());
  ASSERT_TRUE(fixture.host_b->SetScript(ErrorScript(403)).ok());
  fixture.renditions->alternatives.push_back([] {
    StreamSelection selection;
    selection.track = HostTrack("v2", "c");
    selection.base_url = "https://c.test/manifest";
    return selection;
  }());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(20));
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/v1/init.mp4"), 1U);
  EXPECT_EQ(CountUrls(*fixture.transport, "https://b.test/v1b/init.mp4"), 1U);
  EXPECT_EQ(CountUrls(*fixture.transport, "https://c.test/"), 0U);
  ASSERT_THAT(fixture.sink->errors, SizeIs(1));
  EXPECT_EQ(DetailOf(fixture.sink->errors[0]).code,
            base::ErrorCode::kNetworkAccessDenied);
  EXPECT_TRUE(fixture.renditions->excluded.empty());
}

TEST(RecoveryLadderTest, TooManyRequestsWithoutRetryAfterUsesOrdinaryBackoff) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(429)).ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kRetry),
              ElementsAre(500, 1000, 2000));
}

TEST(RecoveryLadderTest, InvalidRetryAfterIsTreatedAsMissing) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a
                  ->SetScript(ErrorScript(
                      429, {{.name = "Retry-After", .value = "tomorrow"}}))
                  .ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kRetry),
              ElementsAre(500, 1000, 2000));
}

TEST(RecoveryLadderTest, ShortRetryAfterIsHonouredAsTheDelay) {
  RecoveryFixture fixture;
  ASSERT_TRUE(
      fixture.host_a
          ->SetScript(ErrorScript(503, {{.name = "Retry-After", .value = "2"}}))
          .ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.transport->TimesMs("init.mp4"),
              ElementsAre(0, 2000, 4000, 6000));
}

TEST(RecoveryLadderTest, LongRetryAfterFailsOverAtOnceAndOutlivesSteering) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 30);
  ASSERT_TRUE(fixture.host_a
                  ->SetScript(ErrorScript(
                      503, {{.name = "Retry-After", .value = "120"}}))
                  .ok());
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  SteeringManifest steering;
  steering.pathway_priority = {"a", "b"};
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Select(HostTrack("v1", "a", 30));
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/v1/init.mp4"), 1U);
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1));
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kRetry), IsEmpty());
  ASSERT_TRUE(fixture.host_a->SetScript(BodyScript()).ok());
  fixture.Advance(absl::Seconds(59));
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  bool excluded = false;
  std::string pathway;
  fixture.Run([&] {
    excluded = fixture.ladder->IsPathwayExcluded("a");
    pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo);
  });
  EXPECT_TRUE(excluded);
  EXPECT_EQ(pathway, "b");
  fixture.policy->Set(Seconds(40), Seconds(40));
  fixture.Reload();
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/v1/init.mp4"), 1U);
  fixture.Advance(absl::Seconds(61));
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Run([&] {
    excluded = fixture.ladder->IsPathwayExcluded("a");
    pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo);
  });
  EXPECT_FALSE(excluded);
  EXPECT_EQ(pathway, "a");
}

TEST(RecoveryLadderTest, MissingSegmentIsFetchedFromAnotherRepresentation) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(404)).ok());
  StreamSelection alternative;
  alternative.track = HostTrack("v2", "c");
  alternative.base_url = "https://c.test/manifest";
  fixture.renditions->alternatives.push_back(alternative);
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(5));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(fixture.sink->tracks, ::testing::Each("v2"));
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/v1/init.mp4"), 1U);
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kRenditionExcluded), 1U);
  EXPECT_THAT(fixture.renditions->excluded, ElementsAre("v1"));
  EXPECT_THAT(fixture.renditions->exclusion_ends, ElementsAre(60'000));
  bool pathway_excluded = true;
  fixture.Run([&] {
    pathway_excluded = fixture.ladder->IsPathwayExcluded("https://a.test");
  });
  EXPECT_FALSE(pathway_excluded);
}

TEST(RecoveryLadderTest, RenditionExclusionLastsTheConfiguredTime) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(404)).ok());
  StreamSelection alternative;
  alternative.track = HostTrack("v2", "c");
  alternative.base_url = "https://c.test/manifest";
  fixture.renditions->alternatives.push_back(alternative);
  RecoveryConfig config;
  config.rendition_exclusion = absl::Seconds(45);
  fixture.Create(config);
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(44));
  EXPECT_THAT(fixture.renditions->excluded, ElementsAre("v1"));
  EXPECT_THAT(fixture.renditions->restored, IsEmpty());
  fixture.Advance(absl::Seconds(2));
  EXPECT_THAT(fixture.renditions->excluded, IsEmpty());
  EXPECT_THAT(fixture.renditions->restored, ElementsAre("v1"));
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kRenditionRestored), 1U);
}

TEST(RecoveryLadderTest, ExhaustedRenditionsEndInAFatalError) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(404)).ok());
  ASSERT_TRUE(fixture.host_c->SetScript(ErrorScript(404)).ok());
  StreamSelection alternative;
  alternative.track = HostTrack("v2", "c");
  alternative.base_url = "https://c.test/manifest";
  fixture.renditions->alternatives.push_back(alternative);
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(5));
  ASSERT_THAT(fixture.sink->errors, SizeIs(1));
  EXPECT_EQ(DetailOf(fixture.sink->errors[0]).code,
            base::ErrorCode::kNetworkRequestFailed);
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kRenditionExcluded), 2U);
  EXPECT_THAT(fixture.sink->sequences, IsEmpty());
}

TEST(RecoveryLadderTest, FailoverDisabledOnlyRetries) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture);
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  RecoveryConfig config;
  config.failover_enabled = false;
  fixture.Create(config);
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(20));
  EXPECT_EQ(CountUrls(*fixture.transport, "https://b.test/"), 0U);
  EXPECT_THAT(fixture.transport->TimesMs("init.mp4"),
              ElementsAre(0, 500, 1500, 3500));
  EXPECT_THAT(fixture.sink->errors, SizeIs(1));
  EXPECT_THAT(fixture.renditions->excluded, IsEmpty());
}

TEST(RecoveryLadderTest, LiveSegmentBehindTheWindowResynchronises) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(404)).ok());
  fixture.probe->position = LivePosition::kBehindWindow;
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(1));
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kResync), 3U);
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kResync),
              ElementsAre(0, 0, 0));
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kRetry), 0U);
  EXPECT_EQ(CountUrls(*fixture.transport, "init.mp4"), 4U);
}

TEST(RecoveryLadderTest, LiveSegmentNearTheEdgeIsRetriedWithBackoff) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(404)).ok());
  fixture.probe->position = LivePosition::kNearEdge;
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(10));
  EXPECT_THAT(fixture.observer->DelaysMs(RecoveryStep::kRetry),
              ElementsAre(500, 1000, 2000));
}

TEST(RecoveryLadderTest, SteeringReorderMovesLaterRequests) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 10);
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  SteeringManifest steering;
  steering.pathway_priority = {"a", "b"};
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Select(HostTrack("v1", "a", 10));
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->tracks, ElementsAre("v1", "v1"));
  steering.pathway_priority = {"b", "a"};
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.policy->Set(Seconds(60), Seconds(60));
  fixture.Reload();
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->sequences, Range(0, 9));
  EXPECT_EQ(fixture.sink->tracks.back(), "v1b");
  EXPECT_EQ(CountUrls(*fixture.transport, "https://a.test/v1/seg2.m4s"), 0U);
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kSteeringApplied), 1U);
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
}

TEST(RecoveryLadderTest, SteeringTtlIsHonouredOnTheFakeClock) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 10);
  fixture.parser->next.ttl = absl::Seconds(30);
  fixture.parser->next.pathway_priority = {"a", "b"};
  fixture.Create();
  std::unique_ptr<SteeringClient> client;
  fixture.Run([&] {
    SteeringClientDependencies dependencies;
    dependencies.transport = fixture.transport;
    dependencies.runner = fixture.runner;
    dependencies.parser = fixture.parser;
    dependencies.listener = fixture.ladder;
    SteeringClientConfig config;
    config.uri = "https://steer.test/steer";
    auto created = SteeringClient::Create(std::move(dependencies), config);
    ASSERT_TRUE(created.ok());
    client = *std::move(created);
    client->Start();
  });
  fixture.Settle();
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Select(HostTrack("v1", "a", 10));
  fixture.Advance(absl::Seconds(1));
  std::string pathway;
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "a");
  fixture.parser->next.pathway_priority = {"b", "a"};
  fixture.Advance(absl::Seconds(28));
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "a");
  fixture.Advance(absl::Seconds(2));
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "b");
  EXPECT_THAT(fixture.transport->TimesMs("steer"), ElementsAre(0, 30'000));
  fixture.Run([&] { client.reset(); });
}

TEST(RecoveryLadderTest, SteeringRefreshReenablesAPathwayExcludedByFailure) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 30);
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  SteeringManifest steering;
  steering.pathway_priority = {"a", "b"};
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Select(HostTrack("v1", "a", 30));
  fixture.Advance(absl::Seconds(1));
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.policy->Set(Seconds(40), Seconds(40));
  fixture.Reload();
  fixture.Advance(absl::Seconds(10));
  std::string pathway;
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "b");
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
  ASSERT_TRUE(fixture.host_a->SetScript(BodyScript()).ok());
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Run(
      [&] { pathway = fixture.ladder->CurrentPathway(StreamKind::kVideo); });
  EXPECT_EQ(pathway, "a");
}

TEST(RecoveryLadderTest,
     FailedPathwayStaysExcludedWithoutSteeringForTheConfiguredTime) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 30);
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.policy->Set(Seconds(8), Seconds(8));
  RecoveryConfig config;
  config.pathway_exclusion = absl::Seconds(100);
  fixture.Create(config);
  fixture.Select(HostTrack("v1", "a", 30));
  fixture.Advance(absl::Seconds(10));
  bool excluded = false;
  fixture.Run([&] { excluded = fixture.ladder->IsPathwayExcluded("a"); });
  EXPECT_TRUE(excluded);
  fixture.Advance(absl::Seconds(92));
  fixture.Run([&] { excluded = fixture.ladder->IsPathwayExcluded("a"); });
  EXPECT_TRUE(excluded);
  fixture.Advance(absl::Seconds(2));
  fixture.Run([&] { excluded = fixture.ladder->IsPathwayExcluded("a"); });
  EXPECT_FALSE(excluded);
}

TEST(RecoveryLadderTest, ContentSteeringDisabledIgnoresTheManifest) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture);
  RecoveryConfig config;
  config.content_steering_enabled = false;
  fixture.Create(config);
  SteeringManifest steering;
  steering.pathway_priority = {"b", "a"};
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Seconds(1));
  EXPECT_EQ(CountUrls(*fixture.transport, "https://b.test/"), 0U);
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kSteeringApplied), 0U);
}

TEST(RecoveryLadderTest, SteeringClonesReachTheRedundancyProvider) {
  RecoveryFixture fixture;
  fixture.Create();
  SteeringManifest steering;
  PathwayClone clone;
  clone.base_id = "a";
  clone.id = "a-clone";
  clone.host = "clone.test";
  steering.pathway_clones.push_back(clone);
  fixture.Run([&] { fixture.ladder->OnSteeringManifest(steering); });
  ASSERT_THAT(fixture.redundancy->cloned, SizeIs(1));
  EXPECT_EQ(fixture.redundancy->cloned[0].id, "a-clone");
}

TEST(RecoveryLadderTest, PlaylistFailoverSignalSwitchesToAnotherPathway) {
  RecoveryFixture fixture;
  AddHlsDuplicates(fixture, 10);
  fixture.policy->Set(Seconds(8), Seconds(8));
  fixture.Create();
  fixture.Select(HostTrack("v1", "a", 10));
  fixture.Advance(absl::Seconds(1));
  bool handled = false;
  fixture.Run([&] {
    handled = fixture.ladder->OnPlaylistFailover(StreamKind::kVideo);
  });
  EXPECT_TRUE(handled);
  EXPECT_EQ(fixture.observer->Count(RecoveryStep::kPlaylistFailover), 1U);
  fixture.policy->Set(Seconds(60), Seconds(60));
  fixture.Reload();
  fixture.Advance(absl::Seconds(1));
  EXPECT_EQ(fixture.sink->tracks.back(), "v1b");
  fixture.Run([&] {
    handled = fixture.ladder->OnPlaylistFailover(StreamKind::kVideo);
  });
  EXPECT_FALSE(handled);
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
}

TEST(RecoveryLadderTest, PlaylistFailoverWithoutAlternativeKeepsPlaying) {
  RecoveryFixture fixture;
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  bool handled = true;
  fixture.Run([&] {
    handled = fixture.ladder->OnPlaylistFailover(StreamKind::kVideo);
  });
  EXPECT_FALSE(handled);
  fixture.Advance(absl::Seconds(1));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(0, 1, 2, 3, 4));
}

TEST(RecoveryLadderTest, ClearingTheTrackCancelsPendingRetries) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Milliseconds(100));
  fixture.Run([&] { fixture.ladder->ClearTrack(StreamKind::kVideo); });
  const size_t total = fixture.transport->log.size();
  fixture.Advance(absl::Seconds(20));
  EXPECT_EQ(fixture.transport->log.size(), total);
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
}

TEST(RecoveryLadderTest, SeekDropsStaleRetries) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.Create();
  fixture.Select(HostTrack("v1", "a"));
  fixture.Advance(absl::Milliseconds(100));
  ASSERT_TRUE(fixture.host_a->SetScript(BodyScript()).ok());
  fixture.Run([&] {
    fixture.ladder->OnSeek();
    fixture.fetcher->Seek(Seconds(8));
  });
  fixture.Advance(absl::Seconds(2));
  EXPECT_THAT(fixture.sink->sequences, ElementsAre(2, 3, 4));
  EXPECT_THAT(fixture.sink->errors, IsEmpty());
}

TEST(RecoveryLadderTest, ErrorsOfUnmanagedStreamsAreForwarded) {
  RecoveryFixture fixture;
  ASSERT_TRUE(fixture.host_a->SetScript(ErrorScript(503)).ok());
  fixture.Create();
  fixture.Run([&] {
    StreamSelection selection;
    selection.track = HostTrack("v1", "a");
    selection.base_url = "https://a.test/manifest";
    EXPECT_TRUE(fixture.fetcher
                    ->SelectTrack(StreamKind::kVideo, selection,
                                  SwitchMode::kAfterInFlight)
                    .ok());
  });
  fixture.Advance(absl::Seconds(1));
  ASSERT_THAT(fixture.sink->errors, SizeIs(1));
  EXPECT_EQ(fixture.sink->errors[0].code(), absl::StatusCode::kUnavailable);
}

TEST(RecoveryLadderTest, SelectTrackNeedsATarget) {
  RecoveryFixture fixture;
  fixture.Create();
  fixture.Run([&] {
    fixture.ladder->SetTarget(nullptr);
    StreamSelection selection;
    selection.track = HostTrack("v1", "a");
    EXPECT_FALSE(fixture.ladder
                     ->SelectTrack(StreamKind::kVideo, selection,
                                   SwitchMode::kAfterInFlight)
                     .ok());
    EXPECT_FALSE(fixture.ladder
                     ->SelectTrack(StreamKind::kVideo, StreamSelection{},
                                   SwitchMode::kAfterInFlight)
                     .ok());
    fixture.ladder->SetTarget(fixture.target.get());
    EXPECT_FALSE(fixture.ladder
                     ->SelectTrack(StreamKind::kVideo, StreamSelection{},
                                   SwitchMode::kAfterInFlight)
                     .ok());
  });
}

}
}
