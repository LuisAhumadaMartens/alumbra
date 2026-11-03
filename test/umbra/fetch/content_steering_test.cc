#include "umbra/fetch/content_steering.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "fetch_test_support.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "recovery_test_support.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace umbra::fetch::test {
namespace {

using ::testing::ElementsAre;

class RecordingListener final : public SteeringListener {
 public:
  void OnSteeringManifest(const SteeringManifest& manifest) override {
    manifests.push_back(manifest);
  }
  void OnSteeringFailure(const absl::Status& status) override {
    failures.push_back(status);
  }

  std::vector<SteeringManifest> manifests;
  std::vector<absl::Status> failures;
};

penumbra::test::HttpScript JsonScript() { return BodyScript(100, 200); }

struct SteeringFixture {
  SteeringFixture()
      : runner(env.CreateSequencedTaskRunner(base::TaskPriority::kControl)),
        transport(std::make_shared<RoutedTransport>(&env.clock())),
        host(std::make_shared<penumbra::test::FakeHttpTransport>()),
        parser(std::make_shared<ScriptedParser>()),
        listener(std::make_shared<RecordingListener>()) {
    transport->Route("https://steer.test/", host);
    EXPECT_TRUE(host->SetScript(JsonScript()).ok());
    parser->next.ttl = absl::Seconds(30);
    parser->next.pathway_priority = {"b", "a"};
  }

  void Create(SteeringClientConfig config = DefaultConfig()) {
    Run([this, config = std::move(config)]() mutable {
      SteeringClientDependencies dependencies;
      dependencies.transport = transport;
      dependencies.runner = runner;
      dependencies.parser = parser;
      dependencies.listener = listener;
      auto created =
          SteeringClient::Create(std::move(dependencies), std::move(config));
      ASSERT_TRUE(created.ok()) << created.status();
      client = *std::move(created);
    });
  }

  static SteeringClientConfig DefaultConfig() {
    SteeringClientConfig config;
    config.uri = "https://steer.test/steer";
    return config;
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    runner->PostTask(std::move(task));
    env.RunUntilIdle();
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

  void Advance(absl::Duration total) {
    Settle();
    for (absl::Duration elapsed; elapsed < total;
         elapsed += absl::Milliseconds(500)) {
      env.FastForwardBy(absl::Milliseconds(500));
      Settle();
    }
  }

  SteeringFixture(const SteeringFixture&) = delete;
  SteeringFixture& operator=(const SteeringFixture&) = delete;

  ~SteeringFixture() {
    Run([this] { client.reset(); });
  }

  base::TestTaskEnvironment env;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<RoutedTransport> transport;
  std::shared_ptr<penumbra::test::FakeHttpTransport> host;
  std::shared_ptr<ScriptedParser> parser;
  std::shared_ptr<RecordingListener> listener;
  std::unique_ptr<SteeringClient> client;
};

TEST(SteeringManifestTest, ValidationChecksVersionTtlAndPriority) {
  EXPECT_TRUE(ValidateSteeringManifest(SteeringManifest{}).ok());
  SteeringManifest manifest;
  manifest.version = 2;
  EXPECT_FALSE(ValidateSteeringManifest(manifest).ok());
  manifest = SteeringManifest{};
  manifest.ttl = absl::Seconds(-1);
  EXPECT_FALSE(ValidateSteeringManifest(manifest).ok());
  manifest = SteeringManifest{};
  manifest.pathway_priority = {"a", "a"};
  EXPECT_FALSE(ValidateSteeringManifest(manifest).ok());
  manifest.pathway_priority = {""};
  EXPECT_FALSE(ValidateSteeringManifest(manifest).ok());
}

TEST(SteeringClientTest, CreateRejectsMissingPiecesAndBadConfig) {
  SteeringClientDependencies dependencies;
  EXPECT_FALSE(
      SteeringClient::Create(dependencies, SteeringFixture::DefaultConfig())
          .ok());
  const SteeringFixture fixture;
  dependencies.transport = fixture.transport;
  dependencies.runner = fixture.runner;
  dependencies.parser = fixture.parser;
  dependencies.listener = fixture.listener;
  SteeringClientConfig config;
  EXPECT_FALSE(SteeringClient::Create(dependencies, config).ok());
  config = SteeringFixture::DefaultConfig();
  config.min_ttl = absl::ZeroDuration();
  EXPECT_FALSE(SteeringClient::Create(dependencies, config).ok());
}

TEST(SteeringClientTest, PollsOnStartAndAgainAfterEachTtl) {
  SteeringFixture fixture;
  fixture.Create();
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Settle();
  EXPECT_EQ(fixture.listener->manifests.size(), 1U);
  fixture.Advance(absl::Seconds(29));
  EXPECT_EQ(fixture.listener->manifests.size(), 1U);
  fixture.Advance(absl::Seconds(1));
  EXPECT_EQ(fixture.listener->manifests.size(), 2U);
  fixture.Advance(absl::Seconds(30));
  EXPECT_EQ(fixture.listener->manifests.size(), 3U);
  EXPECT_THAT(fixture.transport->TimesMs("steer"),
              ElementsAre(0, 30'000, 60'000));
  EXPECT_THAT(fixture.listener->manifests.front().pathway_priority,
              ElementsAre("b", "a"));
}

TEST(SteeringClientTest, ReloadUriReplacesTheRequestUri) {
  SteeringFixture fixture;
  fixture.parser->next.reload_uri = "https://steer.test/next?token=1";
  fixture.Create();
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Advance(absl::Seconds(31));
  EXPECT_THAT(fixture.transport->Urls(),
              ElementsAre("https://steer.test/steer",
                          "https://steer.test/next?token=1"));
}

TEST(SteeringClientTest, TtlBelowTheMinimumIsRaised) {
  SteeringFixture fixture;
  fixture.parser->next.ttl = absl::ZeroDuration();
  fixture.Create();
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Advance(absl::Milliseconds(2500));
  EXPECT_THAT(fixture.transport->TimesMs("steer"), ElementsAre(0, 1000, 2000));
}

TEST(SteeringClientTest, FailureKeepsPollingAfterTheRetryInterval) {
  SteeringFixture fixture;
  ASSERT_TRUE(fixture.host->SetScript(ErrorScript(503)).ok());
  SteeringClientConfig config = SteeringFixture::DefaultConfig();
  config.failure_retry = absl::Seconds(20);
  fixture.Create(config);
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Settle();
  EXPECT_EQ(fixture.listener->failures.size(), 1U);
  EXPECT_TRUE(fixture.listener->manifests.empty());
  ASSERT_TRUE(fixture.host->SetScript(JsonScript()).ok());
  fixture.Advance(absl::Seconds(20));
  EXPECT_EQ(fixture.listener->manifests.size(), 1U);
}

TEST(SteeringClientTest, UnparsableOrInvalidManifestsAreFailures) {
  SteeringFixture fixture;
  fixture.parser->next.version = 9;
  fixture.Create();
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Settle();
  EXPECT_EQ(fixture.listener->failures.size(), 1U);
  EXPECT_TRUE(fixture.listener->manifests.empty());
  EXPECT_EQ(fixture.parser->calls, 1);
}

TEST(SteeringClientTest, StopCancelsFurtherPolling) {
  SteeringFixture fixture;
  fixture.Create();
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Settle();
  fixture.Run([&] { fixture.client->Stop(); });
  fixture.Advance(absl::Seconds(120));
  EXPECT_EQ(fixture.listener->manifests.size(), 1U);
  uint64_t polls = 0;
  fixture.Run([&] { polls = fixture.client->PollCount(); });
  EXPECT_EQ(polls, 1U);
}

TEST(SteeringClientTest, OversizedBodyIsRejected) {
  SteeringFixture fixture;
  SteeringClientConfig config = SteeringFixture::DefaultConfig();
  config.max_body_bytes = 150;
  fixture.Create(config);
  fixture.Run([&] { fixture.client->Start(); });
  fixture.Settle();
  EXPECT_EQ(fixture.listener->failures.size(), 1U);
  EXPECT_EQ(fixture.listener->failures[0].code(),
            absl::StatusCode::kResourceExhausted);
}

}
}
