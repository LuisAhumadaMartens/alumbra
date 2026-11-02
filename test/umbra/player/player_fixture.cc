#include "player_fixture.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/time/time.h"
#include "fake_port.h"
#include "fake_sample_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/task_runner.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"
#include "umbra/player/player_access.h"

namespace umbra::player::test {
namespace {

penumbra::StreamConfig VideoStream() {
  penumbra::StreamConfig config;
  config.info.track_id = kVideoTrack;
  config.info.codec = penumbra::CodecId::kH264;
  config.info.codec_string = "avc1.64001f";
  config.info.time_base = penumbra::Rational::Of(1, 1000);
  penumbra::VideoInfo video;
  video.coded_width = video.display_width = 640;
  video.coded_height = video.display_height = 360;
  config.info.format = video;
  return config;
}

penumbra::StreamConfig AudioStream() {
  auto config = VideoStream();
  config.info.track_id = kAudioTrack;
  config.info.codec = penumbra::CodecId::kAac;
  config.info.format =
      penumbra::AudioInfo{.sample_rate = 48000,
                          .channel_count = 2,
                          .channel_mask = 3,
                          .sample_format = penumbra::SampleFormat::kF32};
  return config;
}

std::shared_ptr<penumbra::PlayerFactory> Borrow(
    penumbra::test::FakePort& port) {
  return {std::shared_ptr<void>(),
          static_cast<penumbra::PlayerFactory*>(&port)};
}

std::shared_ptr<penumbra::CapabilityProvider> BorrowCapabilities(
    penumbra::test::FakePort& port) {
  return {std::shared_ptr<void>(),
          static_cast<penumbra::CapabilityProvider*>(&port)};
}

}

base::ErrorDetail DetailFor(base::ErrorCode code) {
  const auto* entry = base::FindError(code);
  return {code, entry->category, entry->severity, entry->retryable,
          {},   nullptr};
}

PlayerFixture::PlayerFixture()
    : control_(std::make_shared<penumbra::test::RunnerAdapter>(
          env_.CreateSequencedTaskRunner(base::TaskPriority::kControl))),
      port_(control_) {
  options_.preroll_ms = 200;
  options_.allow_playback = [this] { return allow_playback_; };
  options_.on_events_available = [this] { ++notifications_; };
}

void PlayerFixture::TearDown() { Destroy(); }

void PlayerFixture::Do(std::function<void()> task) {
  ASSERT_TRUE(control_->PostTask(std::move(task), absl::ZeroDuration()).ok());
  env_.RunUntilIdle();
}

void PlayerFixture::Create() {
  Do([this] {
    internal::PlayerDependencies dependencies;
    dependencies.factory = Borrow(port_);
    dependencies.capabilities = BorrowCapabilities(port_);
    dependencies.resolve_source =
        [this](
            std::string_view url) -> std::unique_ptr<pipeline::SampleSource> {
      resolved_.emplace_back(url);
      return std::make_unique<pipeline::test::FakeSampleSource>(source_);
    };
    player_ = internal::PlayerAccess::Create(options_, control_,
                                             std::move(dependencies));
  });
}

void PlayerFixture::SetSrc(const std::string& url, int64_t duration_ms) {
  source_ = std::make_shared<pipeline::test::SourceControl>();
  source_->description.streams = {VideoStream(), AudioStream()};
  source_->description.duration =
      penumbra::MediaTime::FromMilliseconds(duration_ms);
  Do([this, url] { player_->set_src(url); });
}

void PlayerFixture::Open() {
  Do([this] { source_->CompleteOpen(); });
}

void PlayerFixture::Grant(uint32_t track, size_t count) {
  Do([this, track, count] {
    ASSERT_TRUE(Fake()->RequestData(track, count).ok());
  });
}

void PlayerFixture::PortState(penumbra::PlayerState state) {
  Do([this, state] { ASSERT_TRUE(Fake()->EmitState(state).ok()); });
}

void PlayerFixture::Preroll(size_t batches) {
  for (size_t i = 0; i < batches; ++i) {
    Grant(kVideoTrack);
    Grant(kAudioTrack);
  }
  PortState(penumbra::PlayerState::kReady);
}

void PlayerFixture::Destroy() {
  Do([this] { player_.reset(); });
}

void PlayerFixture::Dispatch() {
  Do([this] { player_->DispatchEvents(); });
}

void PlayerFixture::StartPlayback() {
  Do([this] {
    player_->Play(
        [this](const PlayResult& result) { results_.push_back(result); });
  });
}

}
