#include "umbra/fetch/segment_fetcher.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
#include "penumbra/interface/crypto_provider.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::fetch::test {
namespace {

using ::testing::ElementsAre;
using ::testing::SizeIs;

class PassThroughDecryptor final : public penumbra::Aes128CbcDecryptor {
 public:
  penumbra::CryptoResult Update(
      std::span<const std::uint8_t> ciphertext) override {
    return penumbra::CryptoResult{.bytes = std::vector<std::uint8_t>(
                                      ciphertext.begin(), ciphertext.end()),
                                  .ok = true};
  }

  penumbra::CryptoResult Finalize() override {
    return penumbra::CryptoResult{.bytes = {}, .ok = true};
  }
};

class PassThroughCryptoProvider final : public penumbra::CryptoProvider {
 public:
  std::unique_ptr<penumbra::Aes128CbcDecryptor> CreateAes128CbcDecryptor(
      std::span<const std::uint8_t> key,
      std::span<const std::uint8_t> iv) override {
    keys.emplace_back(key.begin(), key.end());
    ivs.emplace_back(iv.begin(), iv.end());
    return std::make_unique<PassThroughDecryptor>();
  }

  std::vector<std::vector<std::uint8_t>> keys;
  std::vector<std::vector<std::uint8_t>> ivs;
};

class InvalidPaddingDecryptor final : public penumbra::Aes128CbcDecryptor {
 public:
  penumbra::CryptoResult Update(
      std::span<const std::uint8_t> ciphertext) override {
    return penumbra::CryptoResult{.bytes = {}, .ok = !ciphertext.empty()};
  }

  penumbra::CryptoResult Finalize() override { return {}; }
};

class InvalidPaddingCryptoProvider final : public penumbra::CryptoProvider {
 public:
  std::unique_ptr<penumbra::Aes128CbcDecryptor> CreateAes128CbcDecryptor(
      std::span<const std::uint8_t> key,
      std::span<const std::uint8_t> iv) override {
    if (key.size() != 16 || iv.size() != 16) {
      return nullptr;
    }
    return std::make_unique<InvalidPaddingDecryptor>();
  }
};

std::shared_ptr<const manifest::SegmentIndex> MakeEncryptedIndex(
    std::int64_t sequence, const std::string& track_id,
    const std::string& key_uri = "https://cdn.test/shared.key") {
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = sequence;
  manifest::KeyReference key;
  key.method = manifest::KeyMethod::kAes128;
  key.uri = key_uri;
  key.key_format = "identity";
  parameters.key_references.push_back(key);
  manifest::Segment segment;
  segment.sequence_number = sequence;
  segment.start = penumbra::MediaTime::FromSeconds(sequence);
  segment.duration = penumbra::TimeDelta::FromSeconds(1);
  segment.uri = "https://cdn.test/" + track_id + ".m4s";
  segment.key_index = 0;
  parameters.segments.push_back(segment);
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok()) << index.status();
  return index.ok() ? *index : nullptr;
}

std::shared_ptr<const manifest::SegmentIndex> MakeRotatingIndex() {
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = 1;
  for (const std::uint8_t iv_last_byte : {1, 2}) {
    manifest::KeyReference key;
    key.method = manifest::KeyMethod::kAes128;
    key.uri = "https://cdn.test/rotating.key";
    key.iv = std::vector<std::uint8_t>(16, 0);
    key.iv.back() = iv_last_byte;
    key.key_format = "identity";
    parameters.key_references.push_back(std::move(key));
  }
  for (std::uint32_t key_index = 0; key_index < 2; ++key_index) {
    manifest::Segment segment;
    segment.sequence_number = 1 + key_index;
    segment.start = penumbra::MediaTime::FromSeconds(1 + key_index);
    segment.duration = penumbra::TimeDelta::FromSeconds(1);
    segment.uri =
        "https://cdn.test/rotating" + std::to_string(key_index) + ".m4s";
    segment.key_index = key_index;
    parameters.segments.push_back(std::move(segment));
  }
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok()) << index.status();
  return index.ok() ? *index : nullptr;
}

penumbra::test::HttpScript EncryptedBodyScript() {
  penumbra::test::HttpScript script;
  script.status = 200;
  script.request_start = absl::Milliseconds(10);
  script.response_start = absl::Milliseconds(20);
  script.completion = absl::Milliseconds(30);
  script.chunks.push_back(penumbra::HttpChunk{
      .data = penumbra::SharedBytes::Adopt(std::vector<std::uint8_t>(16, 7)),
      .arrival_time = absl::Milliseconds(25)});
  return script;
}

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

TEST(SegmentFetcherTest, SharedKeyUriIsFetchedOnceAcrossStreams) {
  Fixture fixture;
  auto crypto = std::make_shared<PassThroughCryptoProvider>();
  fixture.sink->stream_media = true;
  ASSERT_TRUE(fixture.transport->SetScript(EncryptedBodyScript()).ok());
  fixture.Run([&fixture, &crypto] {
    FetcherDependencies dependencies;
    dependencies.transport = fixture.transport;
    dependencies.crypto = crypto;
    dependencies.runner = fixture.runner;
    dependencies.sink = fixture.sink;
    dependencies.policy = fixture.policy;
    dependencies.timing = fixture.timing;
    dependencies.resolver = fixture.resolver;
    auto created =
        SegmentFetcher::Create(std::move(dependencies), FetchConfig{});
    ASSERT_TRUE(created.ok()) << created.status();
    fixture.fetcher = *std::move(created);
  });
  fixture.Select(StreamKind::kVideo, MakeTrack("video", StreamKind::kVideo,
                                               MakeEncryptedIndex(1, "video")));
  fixture.Select(StreamKind::kAudio, MakeTrack("audio", StreamKind::kAudio,
                                               MakeEncryptedIndex(2, "audio")));
  fixture.Settle();
  size_t key_requests = 0;
  for (const penumbra::HttpRequest& request : fixture.transport->Requests()) {
    if (request.url == "https://cdn.test/shared.key") {
      ++key_requests;
    }
  }
  EXPECT_EQ(key_requests, 1U);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo), ElementsAre(1));
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kAudio), ElementsAre(2));
  EXPECT_THAT(fixture.sink->media_chunk_sizes, SizeIs(2));
  ASSERT_THAT(crypto->ivs, SizeIs(2));
  EXPECT_EQ(crypto->ivs[0].size(), 16U);
  EXPECT_EQ(crypto->ivs[0][15], 1U);
  EXPECT_EQ(crypto->ivs[1][15], 2U);
}

TEST(SegmentFetcherTest, InvalidPaddingCarriesMediaCorruptDetail) {
  Fixture fixture;
  auto crypto = std::make_shared<InvalidPaddingCryptoProvider>();
  ASSERT_TRUE(fixture.transport->SetScript(EncryptedBodyScript()).ok());
  fixture.Run([&fixture, &crypto] {
    FetcherDependencies dependencies;
    dependencies.transport = fixture.transport;
    dependencies.crypto = crypto;
    dependencies.runner = fixture.runner;
    dependencies.sink = fixture.sink;
    dependencies.policy = fixture.policy;
    dependencies.timing = fixture.timing;
    dependencies.resolver = fixture.resolver;
    auto created =
        SegmentFetcher::Create(std::move(dependencies), FetchConfig{});
    ASSERT_TRUE(created.ok()) << created.status();
    fixture.fetcher = *std::move(created);
  });
  fixture.Select(StreamKind::kVideo, MakeTrack("video", StreamKind::kVideo,
                                               MakeEncryptedIndex(1, "video")));
  fixture.Settle();
  ASSERT_THAT(fixture.sink->errors, SizeIs(1));
  auto detail = base::GetErrorDetail(fixture.sink->errors.front());
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ(detail->value().code, base::ErrorCode::kMediaCorrupt);
  EXPECT_EQ(fixture.sink->errors.front().message(),
            "ALUMBRA-3001: Media data is corrupt");
}

TEST(SegmentFetcherTest, KeyRotationFetchesUpdatedTagForTheSameUri) {
  Fixture fixture;
  auto crypto = std::make_shared<PassThroughCryptoProvider>();
  ASSERT_TRUE(fixture.transport->SetScript(EncryptedBodyScript()).ok());
  fixture.Run([&fixture, &crypto] {
    FetcherDependencies dependencies;
    dependencies.transport = fixture.transport;
    dependencies.crypto = crypto;
    dependencies.runner = fixture.runner;
    dependencies.sink = fixture.sink;
    dependencies.policy = fixture.policy;
    dependencies.timing = fixture.timing;
    dependencies.resolver = fixture.resolver;
    auto created =
        SegmentFetcher::Create(std::move(dependencies), FetchConfig{});
    ASSERT_TRUE(created.ok()) << created.status();
    fixture.fetcher = *std::move(created);
  });
  fixture.Select(StreamKind::kVideo,
                 MakeTrack("video", StreamKind::kVideo, MakeRotatingIndex()));
  fixture.Settle();
  size_t key_requests = 0;
  for (const penumbra::HttpRequest& request : fixture.transport->Requests()) {
    if (request.url == "https://cdn.test/rotating.key") {
      ++key_requests;
    }
  }
  EXPECT_EQ(key_requests, 2U);
  EXPECT_THAT(fixture.sink->MediaSequences(StreamKind::kVideo),
              ElementsAre(1, 2));
  ASSERT_THAT(crypto->ivs, SizeIs(2));
  EXPECT_EQ(crypto->ivs[0][15], 1U);
  EXPECT_EQ(crypto->ivs[1][15], 2U);
}

TEST(SegmentFetcherTest, InsecureKeyUriRequiresOptIn) {
  Fixture fixture;
  fixture.Create();
  auto track =
      MakeTrack("video", StreamKind::kVideo,
                MakeEncryptedIndex(1, "video", "http://cdn.test/shared.key"));
  fixture.Run([&fixture, &track] {
    StreamSelection selection;
    selection.track = track;
    selection.base_url = "https://cdn.test/list.m3u8";
    ASSERT_TRUE(fixture.fetcher
                    ->SelectTrack(StreamKind::kVideo, std::move(selection),
                                  SwitchMode::kAfterInFlight)
                    .ok());
  });
  fixture.Settle();
  EXPECT_THAT(fixture.sink->events, SizeIs(1));
  EXPECT_EQ(fixture.sink->events.front().kind, Event::Kind::kError);
  EXPECT_TRUE(fixture.transport->Requests().empty());
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
