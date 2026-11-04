#include "umbra/pipeline/adaptive_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fake_network.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/fetch/backoff.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline {
namespace {

class EmptyTransport final : public penumbra::HttpTransport {
 public:
  absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> Start(
      penumbra::HttpRequest request,
      std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) override {
    static_cast<void>(request);
    static_cast<void>(callbacks);
    return absl::UnimplementedError("not used");
  }

  absl::Status Stop() override { return absl::OkStatus(); }
};

class RoutedTransport final : public penumbra::HttpTransport {
 public:
  std::shared_ptr<penumbra::test::FakeHttpTransport> primary;
  std::shared_ptr<penumbra::test::FakeHttpTransport> alternative;
  std::shared_ptr<penumbra::test::FakeHttpTransport> steering;

  absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> Start(
      penumbra::HttpRequest request,
      std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) override {
    urls.push_back(request.url);
    if (request.url.starts_with("https://steering.example/")) {
      return steering->Start(std::move(request), std::move(callbacks));
    }
    if (request.url.starts_with("https://primary.example/")) {
      return primary->Start(std::move(request), std::move(callbacks));
    }
    return alternative->Start(std::move(request), std::move(callbacks));
  }

  absl::Status Stop() override { return absl::OkStatus(); }

  std::vector<std::string> urls;
};

class RecordingSink final : public SampleSink {
 public:
  void OnOpened(SourceDescription value) override {
    description = std::move(value);
    opened = true;
  }
  void OnOpenFailed(base::ErrorDetail value) override {
    error = std::move(value);
  }
  void OnDurationChanged(penumbra::MediaTime duration) override {
    changed_duration = duration;
  }
  void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                 std::vector<penumbra::EncodedSample> values) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    for (auto& sample : values) {
      samples.push_back(std::move(sample));
    }
  }
  void OnEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    ended = true;
  }
  void OnSourceError(base::ErrorDetail value) override {
    error = std::move(value);
  }

  SourceDescription description;
  std::vector<penumbra::EncodedSample> samples;
  std::optional<base::ErrorDetail> error;
  std::optional<penumbra::MediaTime> changed_duration;
  bool opened = false;
  bool ended = false;
};

std::shared_ptr<const manifest::Track> MakeVideoTrack(
    const std::string& extension = "m4s", int64_t segment_count = 3,
    std::string id = "video") {
  manifest::ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = 0;
  for (int64_t sequence = 0; sequence < segment_count; ++sequence) {
    manifest::Segment segment;
    segment.sequence_number = sequence;
    segment.start = penumbra::MediaTime::FromSeconds(sequence * 4);
    segment.duration = penumbra::TimeDelta::FromSeconds(4);
    segment.uri = "https://cdn.example/segment" + std::to_string(sequence) +
                  "." + extension;
    parameters.segments.push_back(std::move(segment));
  }
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok());
  manifest::Track track;
  track.id = manifest::TrackId(std::move(id));
  track.bandwidth_bps = 1'000'000;
  manifest::VideoTrackFormat format;
  format.width = 1280;
  format.height = 720;
  format.frame_rate = penumbra::Rational::Of(30, 1);
  track.format = format;
  track.segments = manifest::SegmentIndexProvider::Resolved(*index);
  return manifest::Freeze(std::move(track));
}

std::shared_ptr<const manifest::Track> MakeHlsTrack(std::string id,
                                                    std::string uri,
                                                    std::string pathway) {
  manifest::ExplicitSegmentIndexParameters parameters;
  manifest::Segment segment;
  segment.sequence_number = 0;
  segment.start = penumbra::MediaTime::Zero();
  segment.duration = penumbra::TimeDelta::FromSeconds(4);
  segment.uri = std::move(uri);
  parameters.segments.push_back(std::move(segment));
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  EXPECT_TRUE(index.ok());
  manifest::Track track = *MakeVideoTrack("ts", 1, std::move(id));
  track.segments = manifest::SegmentIndexProvider::Resolved(*index);
  manifest::HlsTrackExtension extension;
  extension.pathway_id = std::move(pathway);
  extension.stable_variant_id = "stable-variant";
  track.extension = std::move(extension);
  return manifest::Freeze(std::move(track));
}

AdaptiveSourceConfig MakeConfig(base::TestTaskEnvironment& environment) {
  manifest::Presentation presentation;
  presentation.protocol = manifest::Protocol::kDash;
  presentation.type = manifest::PresentationType::kStatic;
  presentation.duration = penumbra::TimeDelta::FromSeconds(12);
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  set.tracks.push_back(MakeVideoTrack());
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.push_back(manifest::Freeze(std::move(period)));

  AdaptiveSourceConfig config;
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));
  config.manifest_url = "https://cdn.example/manifest.mpd";
  config.transport = std::make_shared<EmptyTransport>();
  config.runner =
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl);
  config.clock = &environment.clock();
  config.jitter = std::make_shared<fetch::SeededJitter>(1);
  return config;
}

std::vector<uint8_t> ReadFixture(const std::string& name) {
  std::ifstream input(std::string(ALUMBRA_ADAPTIVE_FIXTURE_DIR) + "/" + name,
                      std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

TEST(AdaptiveSourceTest, HlsLiveSourceExposesFetchedSamplesAndSeekableRange) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  manifest::Presentation presentation;
  presentation.protocol = manifest::Protocol::kHls;
  presentation.type = manifest::PresentationType::kDynamic;
  presentation.duration = penumbra::TimeDelta::FromSeconds(4);
  manifest::LiveInfo live;
  live.window_start = penumbra::MediaTime::Zero();
  live.window_end = penumbra::MediaTime::FromSeconds(4);
  presentation.live = live;
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  set.tracks.push_back(MakeVideoTrack("ts", 1));
  set.tracks.push_back(MakeVideoTrack("ts", 1, "video-high"));
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.push_back(manifest::Freeze(std::move(period)));
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));

  auto transport = std::make_shared<penumbra::test::FakeHttpTransport>();
  const std::vector<uint8_t> fixture = ReadFixture("h264_aac.ts");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript script;
  script.request_start = absl::Milliseconds(0);
  script.response_start = absl::Milliseconds(1);
  script.completion = absl::Milliseconds(2);
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Adopt(fixture);
  chunk.arrival_time = absl::Milliseconds(2);
  script.chunks.push_back(std::move(chunk));
  ASSERT_TRUE(transport->SetScript(std::move(script)).ok());
  config.transport = transport;
  auto runner = config.runner;

  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] {
    source->Open(sink);
    EXPECT_TRUE(source
                    ->SelectRendition(fetch::StreamKind::kVideo,
                                      manifest::TrackId("video-high"))
                    .ok());
  });
  environment.RunUntilIdle();
  transport->Pump();
  environment.RunUntilIdle();

  ASSERT_TRUE(sink->opened);
  ASSERT_EQ(sink->description.streams.size(), 1U);
  EXPECT_EQ(sink->description.streams.front().info.type(),
            penumbra::StreamType::kVideo);
  EXPECT_EQ(sink->description.duration, penumbra::MediaTime::Infinite());
  ASSERT_TRUE(sink->description.seekable_range.has_value());
  EXPECT_EQ(sink->description.seekable_range->start,
            penumbra::MediaTime::Zero());
  EXPECT_EQ(sink->description.seekable_range->end,
            penumbra::MediaTime::FromSeconds(4));
  runner->PostTask([&source] {
    source->RequestSamples(1, 1, std::numeric_limits<size_t>::max());
  });
  environment.RunUntilIdle();
  EXPECT_FALSE(sink->samples.empty());
  EXPECT_EQ(sink->samples.front().track_id, 1U);
  EXPECT_FALSE(sink->error.has_value());
}

TEST(AdaptiveSourceTest, DemuxesAndDeliversFetchedDashSegment) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  auto transport = std::make_shared<penumbra::test::FakeHttpTransport>();
  const std::vector<uint8_t> fixture = ReadFixture("fragmented_sidx.mp4");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript script;
  script.request_start = absl::Milliseconds(0);
  script.response_start = absl::Milliseconds(1);
  script.completion = absl::Milliseconds(2);
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Adopt(fixture);
  chunk.arrival_time = absl::Milliseconds(2);
  script.chunks.push_back(std::move(chunk));
  ASSERT_TRUE(transport->SetScript(std::move(script)).ok());
  config.transport = transport;
  auto runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  transport->Pump();
  environment.RunUntilIdle();

  ASSERT_TRUE(sink->opened);
  ASSERT_EQ(sink->description.streams.size(), 1U);
  EXPECT_EQ(sink->description.streams.front().info.type(),
            penumbra::StreamType::kVideo);
  runner->PostTask([&source] {
    source->RequestSamples(1, 1, std::numeric_limits<size_t>::max());
  });
  environment.RunUntilIdle();
  EXPECT_FALSE(sink->samples.empty());
  EXPECT_EQ(sink->samples.front().track_id, 1U);
  EXPECT_FALSE(sink->error.has_value());
}

TEST(AdaptiveSourceTest, Primary503ContinuesOnAlternativeBaseUrl) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  manifest::Presentation presentation = *config.presentation;
  manifest::Track track = *MakeVideoTrack("ts", 1);
  manifest::ExplicitSegmentIndexParameters parameters;
  manifest::Segment segment;
  segment.sequence_number = 0;
  segment.start = penumbra::MediaTime::Zero();
  segment.duration = penumbra::TimeDelta::FromSeconds(4);
  segment.uri = "segment0.ts";
  parameters.segments.push_back(std::move(segment));
  auto index = manifest::ExplicitSegmentIndex::Create(std::move(parameters));
  ASSERT_TRUE(index.ok());
  track.segments = manifest::SegmentIndexProvider::Resolved(*index);
  manifest::DashTrackExtension extension;
  extension.base_urls = {"https://primary.example/",
                         "https://alternative.example/"};
  track.extension = std::move(extension);
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  set.tracks.push_back(manifest::Freeze(std::move(track)));
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.clear();
  presentation.periods.push_back(manifest::Freeze(std::move(period)));
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));
  config.recovery.backoff.max_attempts = 0;

  auto transport = std::make_shared<RoutedTransport>();
  transport->primary = std::make_shared<penumbra::test::FakeHttpTransport>();
  transport->alternative =
      std::make_shared<penumbra::test::FakeHttpTransport>();
  penumbra::test::HttpScript failed;
  failed.status = 503;
  ASSERT_TRUE(transport->primary->SetScript(std::move(failed)).ok());
  const std::vector<uint8_t> fixture = ReadFixture("h264_aac.ts");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript successful;
  successful.response_start = absl::Milliseconds(1);
  successful.completion = absl::Milliseconds(2);
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Adopt(fixture);
  chunk.arrival_time = absl::Milliseconds(2);
  successful.chunks.push_back(std::move(chunk));
  ASSERT_TRUE(transport->alternative->SetScript(std::move(successful)).ok());
  config.transport = transport;
  auto runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  transport->primary->Pump();
  environment.RunUntilIdle();
  transport->alternative->Pump();
  environment.RunUntilIdle();

  EXPECT_EQ(transport->urls, (std::vector<std::string>{
                                 "https://primary.example/segment0.ts",
                                 "https://alternative.example/segment0.ts"}));
  EXPECT_TRUE(sink->opened);
  EXPECT_FALSE(sink->error.has_value());
}

TEST(AdaptiveSourceTest, Primary503ContinuesOnRedundantHlsVariant) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  manifest::Presentation presentation = *config.presentation;
  presentation.protocol = manifest::Protocol::kHls;
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  set.tracks.push_back(
      MakeHlsTrack("primary", "https://primary.example/segment.ts", "primary"));
  set.tracks.push_back(MakeHlsTrack(
      "alternative", "https://alternative.example/segment.ts", "alternative"));
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.clear();
  presentation.periods.push_back(manifest::Freeze(std::move(period)));
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));
  config.recovery.backoff.max_attempts = 0;

  auto transport = std::make_shared<RoutedTransport>();
  transport->primary = std::make_shared<penumbra::test::FakeHttpTransport>();
  transport->alternative =
      std::make_shared<penumbra::test::FakeHttpTransport>();
  penumbra::test::HttpScript failed;
  failed.status = 503;
  ASSERT_TRUE(transport->primary->SetScript(std::move(failed)).ok());
  const std::vector<uint8_t> fixture = ReadFixture("h264_aac.ts");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript successful;
  successful.response_start = absl::Milliseconds(1);
  successful.completion = absl::Milliseconds(2);
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Adopt(fixture);
  chunk.arrival_time = absl::Milliseconds(2);
  successful.chunks.push_back(std::move(chunk));
  ASSERT_TRUE(transport->alternative->SetScript(std::move(successful)).ok());
  config.transport = transport;
  auto runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  transport->primary->Pump();
  environment.RunUntilIdle();
  transport->alternative->Pump();
  environment.RunUntilIdle();

  EXPECT_EQ(transport->urls, (std::vector<std::string>{
                                 "https://primary.example/segment.ts",
                                 "https://alternative.example/segment.ts"}));
  EXPECT_TRUE(sink->opened);
  EXPECT_FALSE(sink->error.has_value());
}

TEST(AdaptiveSourceTest, MissingHlsRepresentationUsesAnotherRendition) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  manifest::Presentation presentation = *config.presentation;
  presentation.protocol = manifest::Protocol::kHls;
  manifest::Track first =
      *MakeHlsTrack("missing", "https://primary.example/missing.ts", "primary");
  manifest::Track alternative = *MakeHlsTrack(
      "available", "https://alternative.example/segment.ts", "alternative");
  first.bandwidth_bps = 1'000'000;
  alternative.bandwidth_bps = 500'000;
  std::get<manifest::HlsTrackExtension>(alternative.extension)
      .stable_variant_id.reset();
  std::get<manifest::HlsTrackExtension>(first.extension)
      .stable_variant_id.reset();
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  set.tracks.push_back(manifest::Freeze(std::move(first)));
  set.tracks.push_back(manifest::Freeze(std::move(alternative)));
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.clear();
  presentation.periods.push_back(manifest::Freeze(std::move(period)));
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));
  config.recovery.backoff.max_attempts = 0;

  auto transport = std::make_shared<RoutedTransport>();
  transport->primary = std::make_shared<penumbra::test::FakeHttpTransport>();
  transport->alternative =
      std::make_shared<penumbra::test::FakeHttpTransport>();
  penumbra::test::HttpScript missing;
  missing.status = 404;
  ASSERT_TRUE(transport->primary->SetScript(std::move(missing)).ok());
  const std::vector<uint8_t> fixture = ReadFixture("h264_aac.ts");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript successful;
  successful.response_start = absl::Milliseconds(1);
  successful.completion = absl::Milliseconds(2);
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Adopt(fixture);
  chunk.arrival_time = absl::Milliseconds(2);
  successful.chunks.push_back(std::move(chunk));
  ASSERT_TRUE(transport->alternative->SetScript(std::move(successful)).ok());
  config.transport = transport;
  auto runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  transport->primary->Pump();
  environment.RunUntilIdle();
  transport->alternative->Pump();
  environment.RunUntilIdle();

  EXPECT_EQ(transport->urls, (std::vector<std::string>{
                                 "https://primary.example/missing.ts",
                                 "https://alternative.example/segment.ts"}));
  EXPECT_TRUE(sink->opened);
  EXPECT_FALSE(sink->error.has_value());
}

TEST(AdaptiveSourceTest, SteeringCloneRedirectsRedundantSegments) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  manifest::Presentation presentation = *config.presentation;
  presentation.protocol = manifest::Protocol::kHls;
  manifest::HlsPresentationExtension hls;
  hls.content_steering = manifest::HlsContentSteering{
      .server_uri = "https://steering.example/steer", .pathway_id = "primary"};
  presentation.hls = std::move(hls);
  manifest::ExplicitSegmentIndexParameters index_parameters;
  for (std::int64_t sequence = 0; sequence < 2; ++sequence) {
    manifest::Segment segment;
    segment.sequence_number = sequence;
    segment.start = penumbra::MediaTime::FromSeconds(sequence * 4);
    segment.duration = penumbra::TimeDelta::FromSeconds(4);
    segment.uri =
        "https://primary.example/segment" + std::to_string(sequence) + ".ts";
    index_parameters.segments.push_back(std::move(segment));
  }
  auto index =
      manifest::ExplicitSegmentIndex::Create(std::move(index_parameters));
  ASSERT_TRUE(index.ok());
  manifest::SwitchingSet set;
  set.id = manifest::SwitchingSetId("video");
  set.type = penumbra::StreamType::kVideo;
  manifest::Track track =
      *MakeHlsTrack("video", "https://primary.example/segment0.ts", "primary");
  track.segments = manifest::SegmentIndexProvider::Resolved(*index);
  set.tracks.push_back(manifest::Freeze(std::move(track)));
  manifest::Period period;
  period.id = manifest::PeriodId("period");
  period.start = penumbra::MediaTime::Zero();
  period.duration = presentation.duration;
  period.switching_sets.push_back(manifest::Freeze(std::move(set)));
  presentation.periods.clear();
  presentation.periods.push_back(manifest::Freeze(std::move(period)));
  config.presentation =
      std::make_shared<const manifest::Presentation>(std::move(presentation));
  config.recovery.backoff.max_attempts = 0;
  config.buffer.forward_goal_ms = 4000;

  auto transport = std::make_shared<RoutedTransport>();
  transport->primary = std::make_shared<penumbra::test::FakeHttpTransport>();
  transport->alternative =
      std::make_shared<penumbra::test::FakeHttpTransport>();
  transport->steering = std::make_shared<penumbra::test::FakeHttpTransport>();
  const std::vector<uint8_t> fixture = ReadFixture("h264_aac.ts");
  ASSERT_FALSE(fixture.empty());
  penumbra::test::HttpScript successful;
  successful.response_start = absl::Milliseconds(1);
  successful.completion = absl::Milliseconds(2);
  penumbra::HttpChunk media_chunk;
  media_chunk.data = penumbra::SharedBytes::Adopt(fixture);
  media_chunk.arrival_time = absl::Milliseconds(2);
  successful.chunks.push_back(std::move(media_chunk));
  ASSERT_TRUE(transport->primary->SetScript(successful).ok());
  ASSERT_TRUE(transport->alternative->SetScript(std::move(successful)).ok());
  const std::string json =
      R"({"VERSION":1,"TTL":30,"PATHWAY-PRIORITY":["clone","primary"],"PATHWAY-CLONES":[{"BASE-ID":"primary","ID":"clone","URI-REPLACEMENT":{"HOST":"alternative.example"}}]})";
  penumbra::test::HttpScript steering_response;
  penumbra::HttpChunk steering_chunk;
  steering_chunk.data = penumbra::SharedBytes::Copy(
      std::span(reinterpret_cast<const uint8_t*>(json.data()), json.size()));
  steering_response.chunks.push_back(std::move(steering_chunk));
  ASSERT_TRUE(
      transport->steering->SetScript(std::move(steering_response)).ok());
  config.transport = transport;
  auto runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  transport->steering->Pump();
  environment.RunUntilIdle();
  transport->primary->Pump();
  environment.RunUntilIdle();
  ASSERT_TRUE(sink->opened);
  runner->PostTask([&source] {
    source->RequestSamples(1, 1, std::numeric_limits<size_t>::max());
  });
  environment.RunUntilIdle();
  transport->alternative->Pump();
  environment.RunUntilIdle();

  EXPECT_EQ(transport->urls,
            (std::vector<std::string>{
                "https://steering.example/steer?_HLS_pathway=primary",
                "https://primary.example/segment0.ts",
                "https://alternative.example/segment1.ts"}));
  EXPECT_TRUE(sink->opened);
  EXPECT_FALSE(sink->error.has_value());
  runner->PostTask([&source] { source->Close(); });
  environment.RunUntilIdle();
}

TEST(AdaptiveSourceTest, AccurateSeekKeepsRequestedPosition) {
  base::TestTaskEnvironment environment;
  auto source = AdaptiveSource::Create(MakeConfig(environment));
  ASSERT_TRUE(source.ok()) << source.status();
  const SeekPoint point =
      (*source)->ResolveSeek(penumbra::MediaTime::FromMilliseconds(5500),
                             SeekMode::kAccurate, FastSeekSnap::kPrevious);
  EXPECT_EQ(point.presentation_time,
            penumbra::MediaTime::FromMilliseconds(5500));
  EXPECT_EQ(point.decode_time, penumbra::MediaTime::FromMilliseconds(5500));
}

TEST(AdaptiveSourceTest, FastSeekChoosesContainingSegmentBoundary) {
  base::TestTaskEnvironment environment;
  auto source = AdaptiveSource::Create(MakeConfig(environment));
  ASSERT_TRUE(source.ok()) << source.status();
  const SeekPoint point =
      (*source)->ResolveSeek(penumbra::MediaTime::FromMilliseconds(5500),
                             SeekMode::kFast, FastSeekSnap::kPrevious);
  EXPECT_EQ(point.presentation_time,
            penumbra::MediaTime::FromMilliseconds(5500));
  EXPECT_EQ(point.decode_time, penumbra::MediaTime::FromSeconds(4));
}

TEST(AdaptiveSourceTest, FastSeekNearestChoosesNearestSegmentBoundary) {
  base::TestTaskEnvironment environment;
  auto source = AdaptiveSource::Create(MakeConfig(environment));
  ASSERT_TRUE(source.ok()) << source.status();
  const SeekPoint point =
      (*source)->ResolveSeek(penumbra::MediaTime::FromMilliseconds(6500),
                             SeekMode::kFast, FastSeekSnap::kNearest);
  EXPECT_EQ(point.decode_time, penumbra::MediaTime::FromSeconds(8));
}

TEST(AdaptiveSourceTest, LiveDurationIsReportedAsInfinite) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  auto presentation =
      std::make_shared<manifest::Presentation>(*config.presentation);
  presentation->type = manifest::PresentationType::kDynamic;
  presentation->duration = penumbra::TimeDelta::Unknown();
  presentation->live = manifest::LiveInfo{};
  config.presentation = std::move(presentation);
  auto source = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(source.ok()) << source.status();
  const SeekPoint point =
      (*source)->ResolveSeek(penumbra::MediaTime::FromSeconds(1),
                             SeekMode::kFast, FastSeekSnap::kPrevious);
  EXPECT_EQ(point.decode_time, penumbra::MediaTime::Zero());
}

TEST(AdaptiveSourceTest, DashUtcTimingCompletesBeforeAnySegmentRequest) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  auto presentation =
      std::make_shared<manifest::Presentation>(*config.presentation);
  presentation->type = manifest::PresentationType::kDynamic;
  presentation->duration = penumbra::TimeDelta::Unknown();
  manifest::LiveInfo live;
  live.availability_start_time = absl::UnixEpoch();
  live.window_start = penumbra::MediaTime::Zero();
  live.window_end = penumbra::MediaTime::FromSeconds(12);
  live.time_shift_depth = penumbra::TimeDelta::FromSeconds(10);
  live.suggested_presentation_delay = penumbra::TimeDelta::FromSeconds(2);
  live.target_segment_duration = penumbra::TimeDelta::FromSeconds(4);
  presentation->live = live;
  manifest::DashPresentationExtension dash;
  dash.utc_timings.push_back(
      {"urn:mpeg:dash:utc:http-xsdate:2014", "https://time.example/utc"});
  presentation->dash_extension = dash;
  config.presentation = presentation;
  config.wall_clock = [] { return absl::UnixEpoch() - absl::Seconds(20); };
  auto transport = std::make_shared<penumbra::test::FakeHttpTransport>();
  penumbra::test::HttpScript script;
  const std::string time_body = "1970-01-01T00:00:10Z";
  script.chunks.push_back(
      {.data = penumbra::SharedBytes::Adopt(
           std::vector<std::uint8_t>(time_body.begin(), time_body.end())),
       .arrival_time = absl::Milliseconds(1)});
  script.request_start = absl::Milliseconds(0);
  script.response_start = absl::Milliseconds(1);
  script.completion = absl::Milliseconds(2);
  ASSERT_TRUE(transport->SetScript(std::move(script)).ok());
  config.transport = transport;
  const std::shared_ptr<base::SequencedTaskRunner> runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  const std::vector<penumbra::HttpRequest> before_sync = transport->Requests();
  ASSERT_EQ(before_sync.size(), 1U);
  EXPECT_EQ(before_sync.front().request_class,
            penumbra::RequestClass::kManifest);
  EXPECT_EQ(before_sync.front().url, "https://time.example/utc");
  transport->Pump();
  environment.RunUntilIdle();
  const std::vector<penumbra::HttpRequest> after_sync = transport->Requests();
  ASSERT_GT(after_sync.size(), 1U);
  EXPECT_TRUE(std::any_of(after_sync.begin() + 1, after_sync.end(),
                          [](const penumbra::HttpRequest& request) {
                            return request.url ==
                                   "https://cdn.example/segment2.m4s";
                          }));
  EXPECT_TRUE(std::all_of(after_sync.begin() + 1, after_sync.end(),
                          [](const penumbra::HttpRequest& request) {
                            return request.request_class ==
                                   penumbra::RequestClass::kMedia;
                          }));
  runner->PostTask([&source] { source->Close(); });
  environment.RunUntilIdle();
}

TEST(AdaptiveSourceTest, DynamicToStaticUpdateReportsTheFinalDuration) {
  base::TestTaskEnvironment environment;
  AdaptiveSourceConfig config = MakeConfig(environment);
  auto dynamic = std::make_shared<manifest::Presentation>(*config.presentation);
  dynamic->type = manifest::PresentationType::kDynamic;
  dynamic->duration = penumbra::TimeDelta::Unknown();
  manifest::LiveInfo live;
  live.availability_start_time = absl::UnixEpoch();
  live.window_start = penumbra::MediaTime::Zero();
  live.window_end = penumbra::MediaTime::FromSeconds(12);
  live.time_shift_depth = penumbra::TimeDelta::FromSeconds(10);
  live.suggested_presentation_delay = penumbra::TimeDelta::FromSeconds(2);
  live.target_segment_duration = penumbra::TimeDelta::FromSeconds(4);
  dynamic->live = live;
  config.presentation = dynamic;
  config.wall_clock = [] { return absl::UnixEpoch() + absl::Seconds(10); };
  const std::shared_ptr<base::SequencedTaskRunner> runner = config.runner;
  auto created = AdaptiveSource::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.status();
  auto source = std::move(*created);
  auto sink = std::make_shared<RecordingSink>();
  runner->PostTask([&source, sink] { source->Open(sink); });
  environment.RunUntilIdle();
  auto ended = std::make_shared<manifest::Presentation>(*dynamic);
  ended->type = manifest::PresentationType::kStatic;
  ended->duration = penumbra::TimeDelta::FromSeconds(12);
  ended->live.reset();
  runner->PostTask([&source, ended] {
    EXPECT_TRUE(source->UpdatePresentation(ended).ok());
  });
  environment.RunUntilIdle();
  ASSERT_TRUE(sink->changed_duration.has_value());
  EXPECT_EQ(*sink->changed_duration, penumbra::MediaTime::FromSeconds(12));
  source->Close();
}

}
}
