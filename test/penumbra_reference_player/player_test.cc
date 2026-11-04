#include "penumbra/interface/player.h"

#include <absl/time/time.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/fallback_demuxer.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/stream_provider.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra/reference_player/player.h"
#include "penumbra_conformance/control_runner_adapter.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"

namespace penumbra {
namespace {

class TestVideoSink final : public VideoSink {
 public:
  absl::Status Start(std::shared_ptr<VideoFrameSource> frames) override {
    if (frames == nullptr) {
      return absl::InvalidArgumentError("missing frames");
    }
    frames_ = std::move(frames);
    return absl::OkStatus();
  }
  absl::Status SetBounds(OutputBounds bounds) override {
    static_cast<void>(bounds);
    return absl::OkStatus();
  }
  absl::Status Stop() override {
    frames_.reset();
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<VideoFrameSource> frames_;
};

class SyntheticDecoder final : public Decoder {
 public:
  SyntheticDecoder(StreamConfig stream, SeekTicket ticket,
                   std::shared_ptr<DecoderCallbacks> callbacks,
                   std::shared_ptr<ControlTaskRunner> control)
      : stream_(std::move(stream)),
        ticket_(ticket),
        callbacks_(std::move(callbacks)),
        control_(std::move(control)) {
    Request();
  }

  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples) override {
    if (stopped_) {
      return absl::FailedPreconditionError("decoder stopped");
    }
    if (ticket < ticket_) {
      return absl::OkStatus();
    }
    if (ticket != ticket_ || samples.empty()) {
      return absl::InvalidArgumentError("invalid synthetic sample");
    }
    if (!granted_) {
      return absl::FailedPreconditionError("missing grant");
    }
    granted_ = false;
    for (const auto& sample : samples) {
      const auto time = ToMediaTime(sample.pts, Rounding::kNearest);
      if (sample.track_id != stream_.info.track_id || !time.has_value()) {
        return absl::InvalidArgumentError("invalid synthetic sample");
      }
      auto storage = std::make_shared<std::array<uint8_t, 6>>(
          std::array<uint8_t, 6>{16, 128, 128, 128, 128, 128});
      DecodedVideoFrame frame;
      frame.presentation_time = *time;
      frame.duration = TimeDelta::FromMilliseconds(33);
      frame.width = 2;
      frame.height = 2;
      frame.format = PixelFormat::kI420;
      frame.planes = {{{.data = storage->data(), .stride = 2},
                       {.data = &storage->at(4), .stride = 1},
                       {.data = &storage->at(5), .stride = 1}}};
      frame.storage = std::move(storage);
      auto status = callbacks_->OnDecoded(
          {.ticket = ticket_, .detail = std::move(frame)});
      if (!status.ok()) {
        return status;
      }
    }
    Request();
    return absl::OkStatus();
  }

  absl::Status EndOfStream(SeekTicket ticket) override {
    if (ticket != ticket_ || !granted_) {
      return absl::FailedPreconditionError("missing end grant");
    }
    granted_ = false;
    return callbacks_->OnDecoded(
        {.ticket = ticket_,
         .detail = StateChanged{.state = PlayerState::kEndOfStream,
                                .track_id = stream_.info.track_id}});
  }

  absl::Status Flush(SeekTicket ticket) override {
    if (ticket <= ticket_) {
      return absl::InvalidArgumentError("ticket did not advance");
    }
    ticket_ = ticket;
    granted_ = false;
    Request();
    return absl::OkStatus();
  }

  absl::Status Stop() override {
    stopped_ = true;
    callbacks_.reset();
    return absl::OkStatus();
  }

 private:
  void Request() {
    granted_ = true;
    const auto callbacks = callbacks_;
    const SeekTicket ticket = ticket_;
    const uint32_t track_id = stream_.info.track_id;
    static_cast<void>(control_->PostTask(
        [callbacks, ticket, track_id] {
          static_cast<void>(callbacks->OnDecoded(
              {.ticket = ticket,
               .detail = NeedsData{.track_id = track_id, .max_samples = 2}}));
        },
        absl::ZeroDuration()));
  }

  StreamConfig stream_;
  SeekTicket ticket_;
  std::shared_ptr<DecoderCallbacks> callbacks_;
  std::shared_ptr<ControlTaskRunner> control_;
  bool granted_ = false;
  bool stopped_ = false;
};

class SyntheticPort final : public Port {
 public:
  explicit SyntheticPort(std::shared_ptr<ControlTaskRunner> control)
      : control_(std::move(control)) {}
  absl::StatusOr<PortLimits> GetLimits() const override {
    return PortLimits{.api_version = kApiVersion, .max_players = 1};
  }
  [[nodiscard]] BufferBudgets GetBufferBudgets() const override { return {}; }
  [[nodiscard]] absl::StatusOr<CaptionPreferences> GetCaptionPreferences()
      const override {
    return CaptionPreferences{};
  }
  absl::Status SetCaptionPreferencesObserver(
      std::shared_ptr<CaptionPreferencesObserver> observer) override {
    static_cast<void>(observer);
    return absl::OkStatus();
  }
  absl::StatusOr<std::unique_ptr<Player>> CreatePlayer(
      PlayerConfig config,
      std::shared_ptr<PlayerCallbacks> callbacks) override {
    static_cast<void>(config);
    static_cast<void>(callbacks);
    return absl::UnimplementedError("not used");
  }
  absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) override {
    static_cast<void>(observer);
    return absl::OkStatus();
  }
  absl::StatusOr<DecodingCapability> QueryDecoding(
      const CapabilityRequest& request) const override {
    static_cast<void>(request);
    DecodingCapability result;
    result.supported = true;
    return result;
  }
  absl::StatusOr<bool> CanChangeType(const StreamConfig& from,
                                     const StreamConfig& to) const override {
    static_cast<void>(from);
    static_cast<void>(to);
    return false;
  }
  absl::StatusOr<std::vector<DecoderCandidate>> GetDecoderCandidates(
      StreamType type) const override {
    if (type != StreamType::kVideo) {
      return std::vector<DecoderCandidate>{};
    }
    return std::vector<DecoderCandidate>{{.name = "synthetic.video",
                                          .kind = DecoderKind::kBundledSoftware,
                                          .output = DecoderOutput::kCpuBuffer,
                                          .secure = false,
                                          .codecs = {CodecId::kH264},
                                          .max_width = 0,
                                          .max_height = 0,
                                          .quirks = {}}};
  }
  absl::StatusOr<std::unique_ptr<Decoder>> CreateDecoder(
      const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override {
    static_cast<void>(candidate);
    return std::unique_ptr<Decoder>(new SyntheticDecoder(
        std::move(config), ticket, std::move(callbacks), control_));
  }
  absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) override {
    if (mode != OutputMode::kTexture) {
      return absl::UnimplementedError("unsupported output mode");
    }
    return std::unique_ptr<VideoSink>(std::make_unique<TestVideoSink>());
  }
  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override {
    return absl::UnimplementedError("not used");
  }
  absl::StatusOr<std::shared_ptr<ControlTaskRunner>> CreateTaskRunner(
      TaskPriority priority, BlockingPolicy blocking) override {
    static_cast<void>(priority);
    static_cast<void>(blocking);
    return control_;
  }
  absl::StatusOr<std::shared_ptr<HttpTransport>> GetHttpTransport() override {
    return absl::UnimplementedError("not used");
  }
  [[nodiscard]] std::vector<std::shared_ptr<ByteStreamProvider>>
  ByteStreamProviders() const override {
    return {};
  }
  [[nodiscard]] std::vector<std::shared_ptr<ElementaryStreamProvider>>
  ElementaryStreamProviders() const override {
    return {};
  }
  absl::StatusOr<FallbackProbe> ProbeFallbackDemuxer(
      std::span<const uint8_t> window,
      std::string_view name_hint) const override {
    static_cast<void>(window);
    static_cast<void>(name_hint);
    return absl::UnimplementedError("not used");
  }
  absl::StatusOr<std::unique_ptr<FallbackDemuxer>> OpenFallbackDemuxer(
      FallbackDemuxerRequest request,
      std::shared_ptr<FallbackByteSource> source,
      std::shared_ptr<FallbackDemuxerCallbacks> callbacks) override {
    static_cast<void>(request);
    static_cast<void>(source);
    static_cast<void>(callbacks);
    return absl::UnimplementedError("not used");
  }
  absl::Status Log(LogLevel level, std::string_view redacted_message) override {
    static_cast<void>(level);
    static_cast<void>(redacted_message);
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<ControlTaskRunner> control_;
};

class Events final : public PlayerCallbacks {
 public:
  absl::Status OnEvent(PlayerEvent event) override {
    events.push_back(std::move(event));
    return absl::OkStatus();
  }
  std::vector<PlayerEvent> events;
};

StreamConfig VideoConfig() {
  StreamConfig config;
  config.info.track_id = 7;
  config.info.codec = CodecId::kH264;
  config.info.codec_string = "avc1.64001e";
  config.info.time_base = Rational::Of(1, 90000);
  config.info.format = VideoInfo{.coded_width = 2,
                                 .coded_height = 2,
                                 .display_width = 2,
                                 .display_height = 2,
                                 .sample_aspect_ratio = Rational::Of(1, 1),
                                 .frame_rate = Rational{},
                                 .color = ColorSpace{},
                                 .hdr_static_metadata = HdrStaticMetadata{},
                                 .dolby_vision = std::nullopt};
  return config;
}

}

TEST(ReferencePlayerTest, DecodesSyntheticSampleAndRetainsCurrentFrame) {
  umbra::base::TestTaskEnvironment environment;
  auto control = std::make_shared<conformance::ControlRunnerAdapter>(
      environment.CreateSequencedTaskRunner(
          umbra::base::TaskPriority::kControl));
  SyntheticPort port(control);
  auto events = std::make_shared<Events>();
  PlayerConfig config;
  config.streams = {VideoConfig()};
  std::unique_ptr<Player> player;
  ASSERT_TRUE(control
                  ->PostTask(
                      [&] {
                        auto result = CreateReferencePlayer(config, events,
                                                            port, control);
                        ASSERT_TRUE(result.ok()) << result.status();
                        player = std::move(*result);
                      },
                      absl::ZeroDuration())
                  .ok());
  environment.RunUntilIdle();
  ASSERT_NE(player, nullptr);
  ASSERT_TRUE(
      control
          ->PostTask([&] { EXPECT_TRUE(player->SetPlaybackRate(1).ok()); },
                     absl::ZeroDuration())
          .ok());
  environment.RunUntilIdle();
  const auto needs =
      std::ranges::find_if(events->events, [](const auto& event) {
        return std::holds_alternative<NeedsData>(event.detail);
      });
  ASSERT_NE(needs, events->events.end());
  const std::array<uint8_t, 5> bytes{0, 0, 1, 0x65, 0x80};
  EncodedSample sample;
  sample.track_id = 7;
  sample.data = SharedBytes::Copy(bytes);
  sample.pts = {.value = 90000, .time_base = Rational::Of(1, 90000)};
  sample.key_frame = true;
  ASSERT_TRUE(control
                  ->PostTask(
                      [&] {
                        EXPECT_TRUE(
                            player->WriteSamples(1, 7, {&sample, 1}).ok());
                      },
                      absl::ZeroDuration())
                  .ok());
  environment.RunUntilIdle();
  ASSERT_TRUE(control
                  ->PostTask(
                      [&] {
                        auto snapshot = player->GetSnapshot();
                        ASSERT_TRUE(snapshot.ok()) << snapshot.status();
                        EXPECT_EQ(snapshot->frames.decoded, 1U);
                        auto frame = player->GetCurrentFrame(7);
                        ASSERT_TRUE(frame.ok()) << frame.status();
                        ASSERT_TRUE(frame->has_value());
                        EXPECT_EQ((*frame)->presentation_time,
                                  MediaTime::FromSeconds(1));
                        EXPECT_NE((*frame)->storage, nullptr);
                        EXPECT_TRUE(player->Stop().ok());
                      },
                      absl::ZeroDuration())
                  .ok());
  environment.RunUntilIdle();
}

}
