#ifndef TEST_UMBRA_FETCH_RECOVERY_TEST_SUPPORT_H_
#define TEST_UMBRA_FETCH_RECOVERY_TEST_SUPPORT_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "fetch_test_support.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/content_steering.h"
#include "umbra/fetch/recovery_ladder.h"
#include "umbra/fetch/segment_fetcher.h"

namespace umbra::fetch::test {

struct LoggedRequest {
  std::string url;
  absl::Duration at;
};

class RoutedTransport final : public penumbra::HttpTransport {
 public:
  explicit RoutedTransport(const base::TickClock* clock) : clock_(clock) {}

  void Route(const std::string& prefix,
             std::shared_ptr<penumbra::test::FakeHttpTransport> transport) {
    routes_.emplace_back(prefix, std::move(transport));
  }

  absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> Start(
      penumbra::HttpRequest request,
      std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) override {
    log.push_back({.url = request.url, .at = clock_->NowTicks().SinceOrigin()});
    for (const auto& [prefix, transport] : routes_) {
      if (absl::StartsWith(request.url, prefix)) {
        return transport->Start(std::move(request), std::move(callbacks));
      }
    }
    return absl::NotFoundError("no route");
  }

  absl::Status Stop() override { return absl::OkStatus(); }

  size_t Pump() {
    size_t total = 0;
    for (const auto& route : routes_) {
      total += route.second->Pump(1);
    }
    return total;
  }

  [[nodiscard]] std::vector<std::string> Urls() const {
    std::vector<std::string> result;
    result.reserve(log.size());
    for (const LoggedRequest& entry : log) {
      result.push_back(entry.url);
    }
    return result;
  }

  [[nodiscard]] std::vector<int64_t> TimesMs(const std::string& needle) const {
    std::vector<int64_t> result;
    for (const LoggedRequest& entry : log) {
      if (absl::StrContains(entry.url, needle)) {
        result.push_back(absl::ToInt64Milliseconds(entry.at));
      }
    }
    return result;
  }

  std::vector<LoggedRequest> log;

 private:
  const base::TickClock* clock_;
  std::vector<std::pair<std::string,
                        std::shared_ptr<penumbra::test::FakeHttpTransport>>>
      routes_;
};

inline penumbra::test::HttpScript ErrorScript(
    int status, std::vector<penumbra::HttpHeader> headers = {}) {
  penumbra::test::HttpScript script;
  script.status = status;
  script.headers = std::move(headers);
  script.request_start = absl::Milliseconds(10);
  script.response_start = absl::Milliseconds(60);
  script.completion = absl::Milliseconds(60);
  return script;
}

inline penumbra::test::HttpScript FailureScript(absl::Status failure) {
  penumbra::test::HttpScript script;
  script.status = 0;
  script.failure = std::move(failure);
  return script;
}

class FixedJitter final : public JitterSource {
 public:
  explicit FixedJitter(double value) : value_(value) {}
  double NextUnit() override { return value_; }

 private:
  double value_;
};

class ErrorSink final : public SegmentSink {
 public:
  void OnInitSegment(InitSegmentData init) override {
    inits.push_back(init.track.Value());
  }
  void OnMediaSegment(MediaSegmentData media) override {
    sequences.push_back(media.segment.sequence_number);
    tracks.push_back(media.track.Value());
  }
  void OnEndOfStream([[maybe_unused]] StreamKind kind,
                     [[maybe_unused]] uint64_t generation) override {
    ++ends;
  }
  void OnStreamError([[maybe_unused]] StreamKind kind,
                     [[maybe_unused]] uint64_t generation,
                     absl::Status status) override {
    errors.push_back(std::move(status));
  }
  void OnIndexResolved([[maybe_unused]] StreamKind kind,
                       [[maybe_unused]] const manifest::TrackId& track,
                       [[maybe_unused]] const manifest::SegmentIndexProvider&
                           provider) override {}

  std::vector<std::string> inits;
  std::vector<int64_t> sequences;
  std::vector<std::string> tracks;
  std::vector<absl::Status> errors;
  int ends = 0;
};

class FakeRedundancy final : public RedundancyProvider {
 public:
  std::vector<RedundantSource> SourcesFor(
      const manifest::TrackId& track) override {
    const auto found = sources.find(track.Value());
    return found == sources.end() ? std::vector<RedundantSource>{}
                                  : found->second;
  }
  void OnPathwayClones(const std::vector<PathwayClone>& clones) override {
    cloned = clones;
  }

  std::map<std::string, std::vector<RedundantSource>> sources;
  std::vector<PathwayClone> cloned;
};

class FakeRenditions final : public RenditionControl {
 public:
  void ExcludeRendition([[maybe_unused]] StreamKind kind,
                        const manifest::TrackId& track,
                        base::TimeTicks until) override {
    excluded.insert(track.Value());
    exclusion_ends.push_back(absl::ToInt64Milliseconds(until.SinceOrigin()));
  }
  void RestoreRendition([[maybe_unused]] StreamKind kind,
                        const manifest::TrackId& track) override {
    excluded.erase(track.Value());
    restored.push_back(track.Value());
  }
  std::optional<StreamSelection> SelectAlternative(
      [[maybe_unused]] StreamKind kind,
      [[maybe_unused]] const manifest::TrackId& failed) override {
    for (const StreamSelection& candidate : alternatives) {
      if (!excluded.contains(candidate.track->id.Value())) {
        return candidate;
      }
    }
    return std::nullopt;
  }

  std::vector<StreamSelection> alternatives;
  std::set<std::string> excluded;
  std::vector<int64_t> exclusion_ends;
  std::vector<std::string> restored;
};

class RecordingObserver final : public RecoveryObserver {
 public:
  void OnRecoveryEvent(const RecoveryEvent& event) override {
    events.push_back(event);
  }

  [[nodiscard]] size_t Count(RecoveryStep step) const {
    return static_cast<size_t>(std::ranges::count_if(
        events, [step](const RecoveryEvent& e) { return e.step == step; }));
  }

  [[nodiscard]] std::vector<int64_t> DelaysMs(RecoveryStep step) const {
    std::vector<int64_t> result;
    for (const RecoveryEvent& event : events) {
      if (event.step == step) {
        result.push_back(absl::ToInt64Milliseconds(event.delay));
      }
    }
    return result;
  }

  std::vector<RecoveryEvent> events;
};

class ScriptedParser final : public SteeringParser {
 public:
  absl::StatusOr<SteeringManifest> Parse(
      const penumbra::SharedBytes& bytes) override {
    ++calls;
    if (bytes.empty()) {
      return absl::InvalidArgumentError("empty");
    }
    return next;
  }

  SteeringManifest next;
  int calls = 0;
};

class FakeProbe final : public LivePositionProbe {
 public:
  LivePosition Locate([[maybe_unused]] StreamKind kind,
                      [[maybe_unused]] const FetchFailure& failure) override {
    return position;
  }

  LivePosition position = LivePosition::kNotLive;
};

class RecoveryFixture {
 public:
  RecoveryFixture()
      : runner(env.CreateSequencedTaskRunner(base::TaskPriority::kControl)),
        transport(std::make_shared<RoutedTransport>(&env.clock())),
        host_a(std::make_shared<penumbra::test::FakeHttpTransport>()),
        host_b(std::make_shared<penumbra::test::FakeHttpTransport>()),
        host_c(std::make_shared<penumbra::test::FakeHttpTransport>()),
        host_steer(std::make_shared<penumbra::test::FakeHttpTransport>()),
        parser(std::make_shared<ScriptedParser>()),
        sink(std::make_shared<ErrorSink>()),
        policy(std::make_shared<FixedPolicy>()),
        redundancy(std::make_shared<FakeRedundancy>()),
        renditions(std::make_shared<FakeRenditions>()),
        observer(std::make_shared<RecordingObserver>()),
        probe(std::make_shared<FakeProbe>()),
        jitter(std::make_shared<FixedJitter>(1.0)),
        redactor(std::make_shared<base::UrlRedactor>()) {
    policy->Set(Seconds(40), Seconds(40));
    transport->Route("https://a.test/", host_a);
    transport->Route("https://b.test/", host_b);
    transport->Route("https://c.test/", host_c);
    transport->Route("https://steer.test/", host_steer);
    EXPECT_TRUE(host_a->SetScript(BodyScript()).ok());
    EXPECT_TRUE(host_b->SetScript(BodyScript()).ok());
    EXPECT_TRUE(host_c->SetScript(BodyScript()).ok());
    EXPECT_TRUE(host_steer->SetScript(BodyScript(100, 200)).ok());
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    runner->PostTask(std::move(task));
    env.RunUntilIdle();
  }

  void Create(const RecoveryConfig& config = RecoveryConfig{}) {
    Run([this, config] {
      RecoveryDependencies dependencies;
      dependencies.downstream = sink;
      dependencies.runner = runner;
      dependencies.clock = &env.clock();
      dependencies.redactor = redactor;
      dependencies.jitter = jitter;
      dependencies.redundancy = redundancy;
      dependencies.renditions = renditions;
      dependencies.observer = observer;
      dependencies.live = probe;
      auto created = RecoveryLadder::Create(std::move(dependencies), config);
      ASSERT_TRUE(created.ok()) << created.status();
      ladder = *std::move(created);
      FetcherDependencies fetcher_dependencies;
      fetcher_dependencies.transport = transport;
      fetcher_dependencies.runner = runner;
      fetcher_dependencies.sink = ladder;
      fetcher_dependencies.policy = policy;
      auto fetcher_created =
          SegmentFetcher::Create(std::move(fetcher_dependencies), {});
      ASSERT_TRUE(fetcher_created.ok()) << fetcher_created.status();
      fetcher = *std::move(fetcher_created);
      target = std::make_unique<FetcherRecoveryTarget>(fetcher.get());
      ladder->SetTarget(target.get());
    });
  }

  void Select(std::shared_ptr<const manifest::Track> track,
              const std::string& base_url = "https://a.test/manifest") {
    Run([this, track = std::move(track), base_url] {
      StreamSelection selection;
      selection.track = track;
      selection.base_url = base_url;
      EXPECT_TRUE(ladder
                      ->SelectTrack(StreamKind::kVideo, selection,
                                    SwitchMode::kAfterInFlight)
                      .ok());
    });
  }

  void Settle() {
    for (;;) {
      const size_t pumped = transport->Pump();
      env.RunUntilIdle();
      if (pumped == 0) {
        return;
      }
    }
  }

  void Advance(absl::Duration total,
               absl::Duration step = absl::Milliseconds(100)) {
    Settle();
    for (absl::Duration elapsed; elapsed < total; elapsed += step) {
      env.FastForwardBy(step);
      Settle();
    }
  }

  [[nodiscard]] int64_t NowMs() const {
    return absl::ToInt64Milliseconds(env.NowTicks().SinceOrigin());
  }

  void Reload() {
    Run([this] { fetcher->OnBufferChanged(); });
    Settle();
  }

  ~RecoveryFixture() {
    Run([this] {
      ladder->SetTarget(nullptr);
      fetcher.reset();
      target.reset();
    });
  }

  RecoveryFixture(const RecoveryFixture&) = delete;
  RecoveryFixture& operator=(const RecoveryFixture&) = delete;

  base::TestTaskEnvironment env;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<RoutedTransport> transport;
  std::shared_ptr<penumbra::test::FakeHttpTransport> host_a;
  std::shared_ptr<penumbra::test::FakeHttpTransport> host_b;
  std::shared_ptr<penumbra::test::FakeHttpTransport> host_c;
  std::shared_ptr<penumbra::test::FakeHttpTransport> host_steer;
  std::shared_ptr<ScriptedParser> parser;
  std::shared_ptr<ErrorSink> sink;
  std::shared_ptr<FixedPolicy> policy;
  std::shared_ptr<FakeRedundancy> redundancy;
  std::shared_ptr<FakeRenditions> renditions;
  std::shared_ptr<RecordingObserver> observer;
  std::shared_ptr<FakeProbe> probe;
  std::shared_ptr<FixedJitter> jitter;
  std::shared_ptr<const base::UrlRedactor> redactor;
  std::shared_ptr<RecoveryLadder> ladder;
  std::unique_ptr<SegmentFetcher> fetcher;
  std::unique_ptr<FetcherRecoveryTarget> target;
};

inline std::shared_ptr<const manifest::Track> HostTrack(const std::string& id,
                                                        const std::string& host,
                                                        size_t count = 5) {
  return MakeTrack(
      id, StreamKind::kVideo,
      MakeIndex(
          IndexOptions().Base("https://" + host + ".test/" + id).Count(count)));
}

}

#endif
