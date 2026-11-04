#include "player_fixture.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "umbra/player/source_router.h"
#include "umbra/player/text_track_routing.h"
#include "umbra/player/tracks.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

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
    : sequence_runner_(
          env_.CreateSequencedTaskRunner(base::TaskPriority::kControl)),
      control_(
          std::make_shared<penumbra::test::RunnerAdapter>(sequence_runner_)),
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
  Do([this] { player_ = CreateWithSettings(settings_); });
  if (player_ != nullptr) {
    Do([this] {
      ASSERT_FALSE(player_->settings()
                       .Set("buffer.start_threshold_ms", std::int64_t{200})
                       .has_value());
      ASSERT_FALSE(player_->settings()
                       .Set("buffer.rebuffer_threshold_ms", std::int64_t{200})
                       .has_value());
    });
  }
}

std::unique_ptr<Player> PlayerFixture::CreateWithSettings(
    settings::Store& settings) {
  PlayerOptions options = options_;
  options.qoe_enabled = [&settings] {
    return settings.Get(settings::kQoeEnabled);
  };
  internal::PlayerDependencies dependencies;
  dependencies.factory = Borrow(port_);
  dependencies.capabilities = BorrowCapabilities(port_);
  dependencies.resolve_source =
      [this](
          std::string_view url, TextTrackList*,
          internal::TextTrackRoutingOptions,
          const std::vector<std::string>& preferred_languages,
          bool prefer_descriptions) -> std::unique_ptr<pipeline::SampleSource> {
    resolved_.emplace_back(url);
    resolved_audio_languages_ = preferred_languages;
    resolved_prefer_descriptions_ = prefer_descriptions;
    if (source_factory_) {
      return source_factory_(url);
    }
    return std::make_unique<pipeline::test::FakeSampleSource>(source_);
  };
  dependencies.load_text_track = [](std::string, size_t,
                                    internal::TextPayloadCallback callback) {
    callback(
        std::string("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n"
                    "A subtitle\n"));
  };
  return internal::PlayerAccess::Create(std::move(options), control_,
                                        std::move(dependencies));
}

void PlayerFixture::SetSrc(const std::string& url, int64_t duration_ms) {
  source_ = std::make_shared<pipeline::test::SourceControl>();
  source_->description.streams = {VideoStream(), AudioStream()};
  source_->description.audio_tracks = {{.id = "audio-en",
                                        .kind = AudioTrackKind::kMain,
                                        .label = "English",
                                        .language = "en",
                                        .enabled = true},
                                       {.id = "audio-es",
                                        .kind = AudioTrackKind::kAlternative,
                                        .label = "Spanish",
                                        .language = "es",
                                        .enabled = false}};
  source_->description.video_tracks = {{.id = "video-low",
                                        .kind = VideoTrackKind::kMain,
                                        .label = "360p",
                                        .language = {},
                                        .selected = true,
                                        .height = 360,
                                        .bitrate_bps = 500000,
                                        .codec = "avc1.64001f"},
                                       {.id = "video-high",
                                        .kind = VideoTrackKind::kAlternative,
                                        .label = "720p",
                                        .language = {},
                                        .selected = false,
                                        .height = 720,
                                        .bitrate_bps = 1500000,
                                        .codec = "avc1.64001f"}};
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
