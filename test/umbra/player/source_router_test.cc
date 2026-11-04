#include "umbra/player/source_router.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fake_http_transport.h"
#include "fake_stream_provider.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_resolver.h"

namespace umbra::player::internal {
namespace {

class RecordingSink final : public pipeline::SampleSink {
 public:
  void OnOpened(pipeline::SourceDescription value) override {
    description = std::move(value);
  }
  void OnOpenFailed(base::ErrorDetail value) override {
    error = std::move(value);
  }
  void OnDurationChanged(penumbra::MediaTime duration) override {
    static_cast<void>(duration);
  }
  void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                 std::vector<penumbra::EncodedSample> samples) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    static_cast<void>(samples);
  }
  void OnEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
  }
  void OnSourceError(base::ErrorDetail value) override {
    error = std::move(value);
  }

  std::optional<pipeline::SourceDescription> description;
  std::optional<base::ErrorDetail> error;
};

class SourceRouterTest : public ::testing::Test {
 protected:
  SourceRouterTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        redactor_(std::make_shared<const base::UrlRedactor>()) {}

  void VerifyAdaptiveRoute(const std::string& manifest,
                           const std::string& url) {
    const std::string path =
        std::string(ALUMBRA_ROUTER_FIXTURE_DIR) + "/" + manifest;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    provider_ = std::make_shared<source::test::FakeStreamProvider>(
        std::move(bytes), true, true, 4096);
    transport_ = std::make_shared<source::test::FakeHttpTransport>();
    auto registry = std::make_shared<source::SchemeRegistry>();
    ASSERT_TRUE(registry->RegisterByteStream(provider_).ok());
    auto resolver =
        std::make_shared<source::SourceResolver>(registry, redactor_);
    auto create = CreateSourceResolver(
        resolver, runner_,
        std::static_pointer_cast<penumbra::HttpTransport>(transport_),
        redactor_);
    const std::unique_ptr<pipeline::SampleSource> routed = create(url);
    ASSERT_NE(routed, nullptr);
    auto sink = std::make_shared<RecordingSink>();
    ASSERT_TRUE(runner_->PostTask(
        [source = routed.get(), sink] { source->Open(sink); }));
    environment_.RunUntilIdle();
    for (size_t i = 0; i < 20 && transport_->Requests().empty(); ++i) {
      provider_->StepAll();
      environment_.RunUntilIdle();
    }
    EXPECT_FALSE(sink->error.has_value());
    EXPECT_FALSE(transport_->Requests().empty());
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  std::shared_ptr<source::test::FakeStreamProvider> provider_;
  std::shared_ptr<source::test::FakeHttpTransport> transport_;
};

TEST_F(SourceRouterTest, RoutesHlsManifestToAdaptiveFetching) {
  VerifyAdaptiveRoute("apple_low_latency_media.m3u8",
                      "fake://example.test/playlist.m3u8");
}

TEST_F(SourceRouterTest, RoutesDashManifestToAdaptiveFetching) {
  VerifyAdaptiveRoute("list-and-sidx.mpd", "fake://example.test/manifest.mpd");
}

}
}
