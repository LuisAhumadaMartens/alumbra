#include "umbra/fetch/segment_fetcher.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "fetch_test_support.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::fetch::test {
namespace {

using ::testing::ElementsAre;
using ::testing::SizeIs;

TEST(FetchConfigTest, RejectsOutOfRangeValues) {
  EXPECT_TRUE(ValidateFetchConfig(FetchConfig{}).ok());
  FetchConfig config;
  config.max_concurrent_requests = 17;
  EXPECT_FALSE(ValidateFetchConfig(config).ok());
  config = FetchConfig{};
  config.max_concurrent_requests_per_host = 0;
  EXPECT_FALSE(ValidateFetchConfig(config).ok());
  config = FetchConfig{};
  config.init_cache_max_entries = 257;
  EXPECT_FALSE(ValidateFetchConfig(config).ok());
}

TEST(SegmentFetcherTest, VodRequestSequenceMatchesGoldenLog) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("init https://cdn.test/v1/init.mp4",
                          "media https://cdn.test/v1/seg0.m4s",
                          "media https://cdn.test/v1/seg1.m4s",
                          "prefetch https://cdn.test/v1/seg2.m4s",
                          "prefetch https://cdn.test/v1/seg3.m4s"));
  fixture.Settle();
  EXPECT_THAT(fixture.Log(), SizeIs(6));
  EXPECT_EQ(fixture.Log()[5], "prefetch https://cdn.test/v1/seg4.m4s");
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3, 4));
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kInit), 1U);
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kEnd), 1U);
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kIdle);
}

TEST(SegmentFetcherTest, GapSegmentsAreSkippedWithoutRequests) {
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = 0;
  for (std::int64_t sequence = 0; sequence < 3; ++sequence) {
    manifest::Segment segment;
    segment.sequence_number = sequence;
    segment.start = Seconds(sequence * 4);
    segment.duration = penumbra::TimeDelta::FromSeconds(4);
    segment.uri = absl::StrCat("https://cdn.test/v1/seg", sequence, ".m4s");
    segment.gap = sequence == 1;
    parameters.segments.push_back(std::move(segment));
  }
  const auto index =
      manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok()) << index.status();
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, *index));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("media https://cdn.test/v1/seg0.m4s",
                          "prefetch https://cdn.test/v1/seg2.m4s"));
  for (const std::string& request : fixture.Log()) {
    EXPECT_EQ(request.find("seg1.m4s"), std::string::npos);
  }
}

TEST(SegmentFetcherTest, DeliversInSequenceOrderAfterInit) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  fixture.Settle();
  ASSERT_GE(fixture.sink->events.size(), 2U);
  EXPECT_EQ(fixture.sink->events[0].kind, Event::Kind::kInit);
  EXPECT_EQ(fixture.sink->events[1].kind, Event::Kind::kMedia);
  EXPECT_EQ(fixture.sink->events[1].bytes, 1000U);
}

TEST(SegmentFetcherTest, InitSegmentIsFetchedOncePerRepresentation) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  fixture.Settle();
  fixture.Run([&fixture] { fixture.fetcher->Seek(Seconds(0)); });
  fixture.Settle();
  size_t init_requests = 0;
  for (const std::string& line : fixture.Log()) {
    if (line.starts_with("init ")) {
      ++init_requests;
    }
  }
  EXPECT_EQ(init_requests, 1U);
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kInit), 2U);
  EXPECT_TRUE(fixture.sink->events.back().kind == Event::Kind::kEnd);
  size_t cached = 0;
  for (const Event& event : fixture.sink->events) {
    if (event.kind == Event::Kind::kInit && event.from_cache) {
      ++cached;
    }
  }
  EXPECT_EQ(cached, 1U);
  size_t cache_size = 0;
  fixture.Run([&] { cache_size = fixture.fetcher->InitCacheSize(); });
  EXPECT_EQ(cache_size, 1U);
}

TEST(SegmentFetcherTest, SeekDropsEverythingRequestedBeforeIt) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().Count(10))));
  fixture.transport->Pump(3);
  uint64_t generation = 0;
  fixture.Run([&] { generation = fixture.fetcher->Seek(Seconds(24)); });
  fixture.policy->Set(Seconds(32), Seconds(40));
  fixture.Run([&] { fixture.fetcher->OnBufferChanged(); });
  fixture.Settle();
  const std::vector<int64_t> sequences =
      fixture.sink->MediaSequences(StreamKind::kVideo);
  ASSERT_FALSE(sequences.empty());
  EXPECT_EQ(sequences.front(), 6);
  for (const Event& event : fixture.sink->events) {
    EXPECT_EQ(event.generation, generation);
    if (event.kind == Event::Kind::kMedia) {
      EXPECT_GE(event.sequence, 6);
    }
  }
}

TEST(SegmentFetcherTest, SeekCancelsInFlightRequests) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().Count(10))));
  EXPECT_GT(fixture.InFlight(), 0U);
  fixture.policy->Set(Seconds(0), Seconds(0));
  fixture.Run([&] { fixture.fetcher->Seek(Seconds(20)); });
  EXPECT_EQ(fixture.InFlight(), 0U);
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kIdle);
}

TEST(SegmentFetcherTest, ConcurrencyLimitsAreNeverExceeded) {
  Fixture fixture;
  FetchConfig config;
  config.max_concurrent_requests = 3;
  config.max_media_requests = 2;
  config.max_prefetch_requests = 1;
  fixture.Create(config);
  fixture.policy->Set(Seconds(8), Seconds(40));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().Count(10))));
  fixture.Select(
      StreamKind::kAudio,
      MakeTrack(
          "a1", StreamKind::kAudio,
          MakeIndex(IndexOptions().Base("https://cdn.test/a1").Count(10))));
  size_t maximum = 0;
  fixture.Settle(&maximum);
  EXPECT_LE(maximum, 3U);
  EXPECT_GE(maximum, 1U);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo), SizeIs(10));
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kAudio), SizeIs(10));
}

TEST(SegmentFetcherTest, PerHostLimitHoldsRequestsBack) {
  Fixture fixture;
  FetchConfig config;
  config.max_concurrent_requests_per_host = 1;
  fixture.Create(config);
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  EXPECT_EQ(fixture.InFlight(), 1U);
  size_t maximum = 0;
  fixture.Settle(&maximum);
  EXPECT_EQ(maximum, 1U);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3, 4));
}

TEST(SegmentFetcherTest, PerClassCapsApply) {
  Fixture fixture;
  FetchConfig config;
  config.max_media_requests = 1;
  config.max_prefetch_requests = 1;
  fixture.Create(config);
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("init https://cdn.test/v1/init.mp4",
                          "media https://cdn.test/v1/seg0.m4s",
                          "prefetch https://cdn.test/v1/seg2.m4s"));
}

TEST(SegmentFetcherTest, ManifestAndInitRequestsGoBeforeMedia) {
  Fixture fixture;
  FetchConfig config;
  config.max_concurrent_requests = 1;
  fixture.Create(config);
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  EXPECT_THAT(fixture.Log(), ElementsAre("init https://cdn.test/v1/init.mp4"));
}

TEST(SegmentFetcherTest, EveryCompletedRequestEmitsOneTimingSample) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  fixture.Settle();
  EXPECT_THAT(fixture.timing->samples, SizeIs(6));
  size_t media = 0;
  for (const TransferSample& sample : fixture.timing->samples) {
    if (sample.request_class == penumbra::RequestClass::kMedia ||
        sample.request_class == penumbra::RequestClass::kPrefetch) {
      ++media;
      EXPECT_EQ(sample.bytes, 1000U);
      EXPECT_EQ(sample.time_to_first_byte, absl::Milliseconds(50));
      EXPECT_EQ(sample.transfer_time, absl::Milliseconds(200));
      ASSERT_THAT(sample.chunks, SizeIs(2));
      EXPECT_EQ(sample.chunks[0].bytes, 500U);
      EXPECT_EQ(sample.chunks[0].arrival_time, absl::Milliseconds(160));
      EXPECT_EQ(sample.chunks[1].arrival_time, absl::Milliseconds(260));
    }
  }
  EXPECT_EQ(media, 5U);
}

TEST(SegmentFetcherTest, ByteRangesBecomeRangeRequests) {
  Fixture fixture;
  ASSERT_TRUE(fixture.transport->SetScript(BodyScript(250, 206)).ok());
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo,
                           MakeIndex(IndexOptions().Count(2).Ranges(500))));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("init https://cdn.test/v1/init.mp4 [0+500]",
                          "media https://cdn.test/v1/seg0.m4s [0+500]",
                          "media https://cdn.test/v1/seg1.m4s [500+500]"));
  fixture.Settle();
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1));
}

TEST(SegmentFetcherTest, RangeAnsweredWithFullBodyIsAnError) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo,
                           MakeIndex(IndexOptions().Count(1).Ranges())));
  fixture.Settle();
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kError);
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kError), 1U);
}

TEST(SegmentFetcherTest, ShortRangeBodyIsAnError) {
  Fixture fixture;
  ASSERT_TRUE(fixture.transport->SetScript(BodyScript(100, 206)).ok());
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo,
                           MakeIndex(IndexOptions().Count(1).Ranges())));
  fixture.Settle();
  ASSERT_THAT(fixture.sink->events, SizeIs(1));
  EXPECT_EQ(fixture.sink->events[0].code, absl::StatusCode::kDataLoss);
}

TEST(SegmentFetcherTest, HttpErrorStopsTheStreamUntilRetry) {
  Fixture fixture;
  penumbra::test::HttpScript failing = BodyScript();
  failing.status = 404;
  ASSERT_TRUE(fixture.transport->SetScript(failing).ok());
  fixture.Create();
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().NoInit())));
  fixture.Settle();
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kError);
  ASSERT_THAT(fixture.sink->events, SizeIs(1));
  EXPECT_EQ(fixture.sink->events[0].code, absl::StatusCode::kNotFound);
  EXPECT_EQ(fixture.InFlight(), 0U);
  ASSERT_TRUE(fixture.transport->SetScript(BodyScript()).ok());
  fixture.Run([&] { fixture.fetcher->Retry(StreamKind::kVideo); });
  fixture.Settle();
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kIdle);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3, 4));
}

TEST(SegmentFetcherTest, TransportErrorFailsOnlyThatStream) {
  Fixture fixture;
  penumbra::test::HttpScript failing = BodyScript();
  failing.chunks.clear();
  failing.failure = absl::UnavailableError("connection reset");
  failing.completion = failing.response_start;
  ASSERT_TRUE(fixture.transport->SetScript(failing).ok());
  fixture.Create();
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().NoInit())));
  fixture.Settle();
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kError);
  EXPECT_EQ(fixture.State(StreamKind::kAudio), LoaderState::kIdle);
}

TEST(SegmentFetcherTest, GoalsHoldLoadingUntilTheBufferDrains) {
  Fixture fixture;
  fixture.Create();
  fixture.policy->Set(Seconds(4), Seconds(8));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().Count(10))));
  fixture.Settle();
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1));
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kIdle);
  fixture.policy->Set(Seconds(12), Seconds(16));
  fixture.Run([&] { fixture.fetcher->OnBufferChanged(); });
  fixture.Settle();
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3));
}

TEST(SegmentFetcherTest, ByteBudgetLimitsTheQueue) {
  Fixture fixture;
  fixture.Create();
  fixture.policy->windows[0].byte_budget = uint64_t{2'000'000} * 4 / 8 * 2;
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo,
                           MakeIndex(IndexOptions().Count(10).NoInit())));
  EXPECT_THAT(fixture.Log(), SizeIs(2));
  size_t maximum = 0;
  fixture.Settle(&maximum);
  EXPECT_LE(maximum, 2U);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3, 4));
}

TEST(SegmentFetcherTest, PrefetchBecomesMediaWhenDemandGrows) {
  Fixture fixture;
  FetchConfig config;
  config.max_media_requests = 1;
  config.max_prefetch_requests = 1;
  fixture.Create(config);
  fixture.policy->Set(Seconds(4), Seconds(12));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().NoInit())));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("media https://cdn.test/v1/seg0.m4s",
                          "prefetch https://cdn.test/v1/seg1.m4s"));
  fixture.policy->Set(Seconds(20), Seconds(20));
  fixture.Settle();
  EXPECT_THAT(fixture.Log()[2], "media https://cdn.test/v1/seg2.m4s");
}

TEST(SegmentFetcherTest, SwitchAfterInFlightKeepsOrderAndFetchesNewInit) {
  Fixture fixture;
  fixture.Create();
  fixture.policy->Set(Seconds(4), Seconds(4));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo, MakeIndex(IndexOptions().Count(5))));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v2", StreamKind::kVideo,
                MakeIndex(IndexOptions().Base("https://cdn.test/v2").Count(5))),
      SwitchMode::kAfterInFlight);
  fixture.policy->Set(Seconds(20), Seconds(20));
  fixture.Run([&] { fixture.fetcher->OnBufferChanged(); });
  fixture.Settle();
  std::vector<std::string> tracks;
  for (const Event& event : fixture.sink->events) {
    if (event.kind == Event::Kind::kMedia || event.kind == Event::Kind::kInit) {
      tracks.push_back(event.track);
    }
  }
  ASSERT_FALSE(tracks.empty());
  EXPECT_EQ(tracks.front(), "v1");
  EXPECT_EQ(tracks.back(), "v2");
  const auto first_v2 = std::ranges::find(tracks, "v2");
  EXPECT_TRUE(std::ranges::none_of(
      first_v2, tracks.end(),
      [](const std::string& track) { return track == "v1"; }));
  EXPECT_EQ(fixture.sink->MediaSequences(StreamKind::kVideo),
            (std::vector<int64_t>{0, 1, 2, 3, 4}));
}

TEST(SegmentFetcherTest, EmergencySwitchCancelsInFlightRequests) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  fixture.transport->Pump(1);
  fixture.policy->Set(Seconds(4), Seconds(4));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v2", StreamKind::kVideo,
                MakeIndex(IndexOptions().Base("https://cdn.test/v2"))),
      SwitchMode::kCancelInFlight);
  fixture.Settle();
  for (const Event& event : fixture.sink->events) {
    EXPECT_EQ(event.track, "v2");
  }
  EXPECT_FALSE(fixture.sink->events.empty());
  EXPECT_EQ(fixture.sink->events.front().kind, Event::Kind::kInit);
}

TEST(SegmentFetcherTest, SelectingMismatchedTrackFails) {
  Fixture fixture;
  fixture.Create();
  absl::Status status;
  fixture.Run([&] {
    StreamSelection selection;
    selection.track = MakeTrack("a1", StreamKind::kAudio, MakeIndex());
    status = fixture.fetcher->SelectTrack(
        StreamKind::kVideo, std::move(selection), SwitchMode::kAfterInFlight);
  });
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SegmentFetcherTest, DeferredIndexIsResolvedThroughTheResolver) {
  Fixture fixture;
  fixture.resolver->index = MakeIndex(IndexOptions().Count(2).NoInit());
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeDeferredTrack("v1", "https://cdn.test/v1/media.m3u8"));
  EXPECT_THAT(fixture.Log(),
              ElementsAre("manifest https://cdn.test/v1/media.m3u8"));
  EXPECT_EQ(fixture.State(StreamKind::kVideo), LoaderState::kLoadingIndex);
  fixture.Settle();
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kIndex), 1U);
  EXPECT_THAT(fixture.resolver->urls,
              ElementsAre("https://cdn.test/v1/media.m3u8"));
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1));
}

TEST(SegmentFetcherTest, LiveStreamWaitsForNewSegments) {
  Fixture fixture;
  fixture.Create();
  fixture.policy->Set(Seconds(60), Seconds(60));
  auto first = MakeTrack("v1", StreamKind::kVideo,
                         MakeIndex(IndexOptions().Count(2).NoInit()));
  fixture.Select(StreamKind::kVideo, first, SwitchMode::kAfterInFlight, true);
  fixture.Settle();
  EXPECT_EQ(fixture.State(StreamKind::kVideo),
            LoaderState::kWaitingForAvailability);
  EXPECT_EQ(fixture.sink->Count(Event::Kind::kEnd), 0U);
  fixture.Run([&] {
    EXPECT_TRUE(fixture.fetcher
                    ->UpdateTrack(
                        StreamKind::kVideo,
                        MakeTrack("v1", StreamKind::kVideo,
                                  MakeIndex(IndexOptions().Count(4).NoInit())),
                        true)
                    .ok());
  });
  fixture.Settle();
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(0, 1, 2, 3));
  EXPECT_EQ(fixture.State(StreamKind::kVideo),
            LoaderState::kWaitingForAvailability);
}

TEST(SegmentFetcherTest, LiveWindowThatMovedForwardSkipsToItsStart) {
  Fixture fixture;
  fixture.Create();
  fixture.policy->Set(Seconds(80), Seconds(80));
  fixture.Select(
      StreamKind::kVideo,
      MakeTrack("v1", StreamKind::kVideo,
                MakeIndex(IndexOptions().Count(3).First(10).NoInit())),
      SwitchMode::kAfterInFlight, true);
  fixture.Settle();
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(10, 11, 12));
}

TEST(SegmentFetcherTest, ClearTrackCancelsAndIdles) {
  Fixture fixture;
  fixture.Create();
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("v1", StreamKind::kVideo, MakeIndex()));
  fixture.Run([&] { fixture.fetcher->ClearTrack(StreamKind::kVideo); });
  EXPECT_EQ(fixture.InFlight(), 0U);
  fixture.Settle();
  EXPECT_TRUE(fixture.sink->events.empty());
}

TEST(SegmentFetcherTest, RelativeSegmentUrlsResolveAgainstTheBaseUrl) {
  Fixture fixture;
  fixture.Create();
  manifest::ExplicitSegmentIndexParameters parameters;
  manifest::Segment segment;
  segment.start = Seconds(0);
  segment.duration = penumbra::TimeDelta::FromSeconds(4);
  segment.uri = "../media/seg0.m4s";
  parameters.segments.push_back(segment);
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok());
  fixture.Run([&] {
    StreamSelection selection;
    selection.track = MakeTrack("v1", StreamKind::kVideo, *index);
    selection.base_url = "https://cdn.test/final/path/main.m3u8";
    EXPECT_TRUE(fixture.fetcher
                    ->SelectTrack(StreamKind::kVideo, std::move(selection),
                                  SwitchMode::kAfterInFlight)
                    .ok());
  });
  EXPECT_THAT(fixture.Log(),
              ElementsAre("media https://cdn.test/final/media/seg0.m4s"));
}

}
}
