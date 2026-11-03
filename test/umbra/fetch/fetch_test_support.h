#ifndef TEST_UMBRA_FETCH_FETCH_TEST_SUPPORT_H_
#define TEST_UMBRA_FETCH_FETCH_TEST_SUPPORT_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/segment_fetcher.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::fetch::test {

using fetch::StreamKind;
using penumbra::MediaTime;

struct Event {
  enum class Kind { kInit, kMedia, kEnd, kError, kIndex };

  Kind kind = Kind::kMedia;
  StreamKind stream = StreamKind::kVideo;
  uint64_t generation = 0;
  int64_t sequence = 0;
  bool from_cache = false;
  std::string track;
  size_t bytes = 0;
  absl::StatusCode code = absl::StatusCode::kOk;
};

class RecordingSink final : public SegmentSink {
 public:
  void OnInitSegment(InitSegmentData init) override {
    events.push_back({.kind = Event::Kind::kInit,
                      .stream = init.kind,
                      .generation = init.generation,
                      .sequence = 0,
                      .from_cache = init.from_cache,
                      .track = init.track.Value(),
                      .bytes = init.bytes.size(),
                      .code = absl::StatusCode::kOk});
  }
  void OnMediaSegment(MediaSegmentData media) override {
    events.push_back({.kind = Event::Kind::kMedia,
                      .stream = media.kind,
                      .generation = media.generation,
                      .sequence = media.segment.sequence_number,
                      .from_cache = false,
                      .track = media.track.Value(),
                      .bytes = media.bytes.size(),
                      .code = absl::StatusCode::kOk});
  }
  void OnEndOfStream(StreamKind kind, uint64_t generation) override {
    events.push_back({.kind = Event::Kind::kEnd,
                      .stream = kind,
                      .generation = generation,
                      .sequence = 0,
                      .from_cache = false,
                      .track = "",
                      .bytes = 0,
                      .code = absl::StatusCode::kOk});
  }
  void OnStreamError(StreamKind kind, uint64_t generation,
                     absl::Status status) override {
    events.push_back({.kind = Event::Kind::kError,
                      .stream = kind,
                      .generation = generation,
                      .sequence = 0,
                      .from_cache = false,
                      .track = "",
                      .bytes = 0,
                      .code = status.code()});
  }
  void OnIndexResolved(
      StreamKind kind, const manifest::TrackId& track,
      const manifest::SegmentIndexProvider& provider) override {
    events.push_back({.kind = Event::Kind::kIndex,
                      .stream = kind,
                      .generation = 0,
                      .sequence = 0,
                      .from_cache = false,
                      .track = track.Value(),
                      .bytes = provider.is_resolved() ? 1U : 0U,
                      .code = absl::StatusCode::kOk});
  }

  [[nodiscard]] std::vector<int64_t> MediaSequences(StreamKind kind) const {
    std::vector<int64_t> result;
    for (const Event& event : events) {
      if (event.kind == Event::Kind::kMedia && event.stream == kind) {
        result.push_back(event.sequence);
      }
    }
    return result;
  }

  [[nodiscard]] size_t Count(Event::Kind kind) const {
    return static_cast<size_t>(std::ranges::count_if(
        events, [kind](const Event& event) { return event.kind == kind; }));
  }

  std::vector<Event> events;
};

class FixedPolicy final : public LoadPolicy {
 public:
  [[nodiscard]] LoadWindow WindowFor(StreamKind kind) const override {
    return windows.at(static_cast<size_t>(kind));
  }

  void Set(MediaTime demand, MediaTime prefetch) {
    for (LoadWindow& window : windows) {
      window.demand_until = demand;
      window.prefetch_until = prefetch;
    }
  }

  std::array<LoadWindow, kStreamKindCount> windows;
};

class RecordingTiming final : public TransferTimingSink {
 public:
  void OnTransferSample(const TransferSample& sample) override {
    samples.push_back(sample);
  }

  std::vector<TransferSample> samples;
};

class FakeResolver final : public IndexResolver {
 public:
  absl::StatusOr<std::shared_ptr<const manifest::SegmentIndex>> ParseIndex(
      const manifest::IndexLocation& location, const std::string& url,
      const penumbra::SharedBytes& bytes) override {
    urls.push_back(url);
    locations.push_back(location.uri);
    sizes.push_back(bytes.size());
    return index;
  }

  std::shared_ptr<const manifest::SegmentIndex> index;
  std::vector<std::string> urls;
  std::vector<std::string> locations;
  std::vector<size_t> sizes;
};

inline MediaTime Seconds(int64_t seconds) {
  return MediaTime::FromSeconds(seconds);
}

struct IndexOptions {
  std::string base = "https://cdn.test/v1";
  size_t count = 5;
  int64_t first_sequence = 0;
  int64_t duration = 4;
  bool with_init = true;
  bool with_ranges = false;
  int64_t range_length = 1000;

  IndexOptions& Base(std::string value) {
    base = std::move(value);
    return *this;
  }
  IndexOptions& Count(size_t value) {
    count = value;
    return *this;
  }
  IndexOptions& First(int64_t value) {
    first_sequence = value;
    return *this;
  }
  IndexOptions& NoInit() {
    with_init = false;
    return *this;
  }
  IndexOptions& Ranges(int64_t length = 1000) {
    with_ranges = true;
    range_length = length;
    return *this;
  }
};

inline std::shared_ptr<const manifest::SegmentIndex> MakeIndex(
    const IndexOptions& options = IndexOptions()) {
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = options.first_sequence;
  if (options.with_init) {
    manifest::InitSection init;
    init.uri = absl::StrCat(options.base, "/init.mp4");
    if (options.with_ranges) {
      init.byte_range = manifest::ByteRange{.offset = 0, .length = 500};
    }
    parameters.init_sections.push_back(init);
  }
  for (size_t i = 0; i < options.count; ++i) {
    const int64_t sequence = options.first_sequence + static_cast<int64_t>(i);
    manifest::Segment segment;
    segment.sequence_number = sequence;
    segment.start = Seconds(sequence * options.duration);
    segment.duration = penumbra::TimeDelta::FromSeconds(options.duration);
    segment.uri = absl::StrCat(options.base, "/seg", sequence, ".m4s");
    if (options.with_ranges) {
      segment.byte_range = manifest::ByteRange{
          .offset = static_cast<int64_t>(i) * options.range_length,
          .length = options.range_length};
    }
    if (options.with_init) {
      segment.init_index = 0;
    }
    parameters.segments.push_back(std::move(segment));
  }
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok()) << index.status();
  return index.ok() ? *index : nullptr;
}

inline std::shared_ptr<const manifest::Track> MakeTrack(
    const std::string& id, StreamKind kind,
    std::shared_ptr<const manifest::SegmentIndex> index,
    int64_t bandwidth = 2'000'000) {
  manifest::Track track;
  track.id = manifest::TrackId(id);
  track.bandwidth_bps = bandwidth;
  if (kind == StreamKind::kVideo) {
    manifest::VideoTrackFormat video;
    video.width = 1280;
    video.height = 720;
    video.frame_rate = penumbra::Rational::Of(30, 1);
    video.bit_depth = 8;
    track.format = video;
  } else {
    manifest::AudioTrackFormat audio;
    audio.sample_rate = 48'000;
    audio.channels.channel_count = 2;
    track.format = audio;
  }
  track.segments = manifest::SegmentIndexProvider::Resolved(std::move(index));
  return manifest::Freeze(std::move(track));
}

inline std::shared_ptr<const manifest::Track> MakeDeferredTrack(
    const std::string& id, const std::string& uri) {
  manifest::Track track;
  track.id = manifest::TrackId(id);
  track.bandwidth_bps = 2'000'000;
  manifest::VideoTrackFormat video;
  video.width = 1280;
  video.height = 720;
  video.frame_rate = penumbra::Rational::Of(30, 1);
  video.bit_depth = 8;
  track.format = video;
  manifest::IndexLocation location;
  location.uri = uri;
  track.segments = manifest::SegmentIndexProvider::Deferred(location);
  return manifest::Freeze(std::move(track));
}

inline penumbra::test::HttpScript BodyScript(size_t chunk_bytes = 500,
                                             int status = 200) {
  penumbra::test::HttpScript script;
  script.status = status;
  script.request_start = absl::Milliseconds(10);
  script.response_start = absl::Milliseconds(60);
  for (int i = 1; i <= 2; ++i) {
    penumbra::HttpChunk chunk;
    chunk.data = penumbra::SharedBytes::Adopt(
        std::vector<uint8_t>(chunk_bytes, static_cast<uint8_t>(i)));
    chunk.arrival_time = absl::Milliseconds(60 + (100 * i));
    script.chunks.push_back(std::move(chunk));
  }
  script.completion = absl::Milliseconds(260);
  return script;
}

inline const char* ClassName(penumbra::RequestClass request_class) {
  switch (request_class) {
    case penumbra::RequestClass::kManifest:
      return "manifest";
    case penumbra::RequestClass::kLicence:
      return "licence";
    case penumbra::RequestClass::kInit:
      return "init";
    case penumbra::RequestClass::kMedia:
      return "media";
    case penumbra::RequestClass::kPrefetch:
      return "prefetch";
  }
  return "";
}

class Fixture {
 public:
  Fixture()
      : runner(env.CreateSequencedTaskRunner(base::TaskPriority::kControl)),
        transport(std::make_shared<penumbra::test::FakeHttpTransport>()),
        sink(std::make_shared<RecordingSink>()),
        policy(std::make_shared<FixedPolicy>()),
        timing(std::make_shared<RecordingTiming>()),
        resolver(std::make_shared<FakeResolver>()) {
    policy->Set(Seconds(8), Seconds(20));
    EXPECT_TRUE(transport->SetScript(BodyScript()).ok());
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    runner->PostTask(std::move(task));
    env.RunUntilIdle();
  }

  void Create(const FetchConfig& config = {}) {
    Run([this, config] {
      FetcherDependencies dependencies;
      dependencies.transport = transport;
      dependencies.runner = runner;
      dependencies.sink = sink;
      dependencies.policy = policy;
      dependencies.timing = timing;
      dependencies.resolver = resolver;
      auto created = SegmentFetcher::Create(std::move(dependencies), config);
      ASSERT_TRUE(created.ok()) << created.status();
      fetcher = *std::move(created);
    });
  }

  void Select(StreamKind kind, std::shared_ptr<const manifest::Track> track,
              SwitchMode mode = SwitchMode::kAfterInFlight,
              bool dynamic = false) {
    Run([this, kind, track = std::move(track), mode, dynamic] {
      StreamSelection selection;
      selection.track = track;
      selection.dynamic = dynamic;
      EXPECT_TRUE(fetcher->SelectTrack(kind, std::move(selection), mode).ok());
    });
  }

  void Settle(size_t* max_in_flight = nullptr) {
    for (;;) {
      const size_t pumped = transport->Pump(1);
      env.RunUntilIdle();
      if (max_in_flight != nullptr) {
        *max_in_flight = std::max(*max_in_flight, InFlight());
      }
      if (pumped == 0) {
        return;
      }
    }
  }

  size_t InFlight() {
    size_t result = 0;
    Run([this, &result] { result = fetcher->InFlightRequests(); });
    return result;
  }

  LoaderState State(StreamKind kind) {
    LoaderState result = LoaderState::kIdle;
    Run([this, kind, &result] { result = fetcher->State(kind); });
    return result;
  }

  std::vector<std::string> Log() const {
    std::vector<std::string> result;
    for (const penumbra::HttpRequest& request : transport->Requests()) {
      std::string line =
          absl::StrCat(ClassName(request.request_class), " ", request.url);
      if (request.range.has_value()) {
        absl::StrAppend(&line, " [", request.range->offset, "+",
                        request.range->length.value_or(0), "]");
      }
      result.push_back(std::move(line));
    }
    return result;
  }

  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;

  ~Fixture() {
    Run([this] { fetcher.reset(); });
  }

  base::TestTaskEnvironment env;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<penumbra::test::FakeHttpTransport> transport;
  std::shared_ptr<RecordingSink> sink;
  std::shared_ptr<FixedPolicy> policy;
  std::shared_ptr<RecordingTiming> timing;
  std::shared_ptr<FakeResolver> resolver;
  std::unique_ptr<SegmentFetcher> fetcher;
};

}

#endif
