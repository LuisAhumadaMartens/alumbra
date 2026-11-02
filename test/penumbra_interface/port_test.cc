#include "penumbra/interface/port.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/time/time.h"
#include "fake_port.h"
#include "gtest/gtest.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"

namespace penumbra::test {
namespace {

static_assert(!std::is_abstract_v<FakePort>);
static_assert(!std::is_abstract_v<FakePlayer>);
static_assert(!std::is_abstract_v<FakeDecoder>);
static_assert(!std::is_abstract_v<FakeAudioSink>);
static_assert(!std::is_abstract_v<FakeVideoSink>);
static_assert(kApiVersion == 2);

StreamConfig Video(uint32_t track = 7) {
  StreamConfig config;
  config.info.track_id = track;
  config.info.codec = CodecId::kH264;
  config.info.codec_string = "avc1.64001f";
  config.info.time_base = Rational::Of(1, 90000);
  VideoInfo video;
  video.coded_width = video.display_width = 640;
  video.coded_height = video.display_height = 360;
  config.info.format = video;
  return config;
}

StreamConfig Audio() {
  auto config = Video(8);
  config.info.codec = CodecId::kAac;
  config.info.format = AudioInfo{.sample_rate = 48000,
                                 .channel_count = 2,
                                 .channel_mask = 3,
                                 .sample_format = SampleFormat::kF32};
  return config;
}

EncodedSample Sample(uint32_t track = 7) {
  EncodedSample sample;
  sample.track_id = track;
  sample.data = SharedBytes::Adopt({1, 2, 3, 4});
  sample.pts = {.value = 9000, .time_base = Rational::Of(1, 90000)};
  return sample;
}

class Events final : public PlayerCallbacks {
 public:
  absl::Status OnEvent(PlayerEvent event) override {
    EXPECT_TRUE(control->RunsTasksInCurrentSequence());
    events.push_back(std::move(event));
    if (on_event) {
      on_event();
    }
    return result;
  }
  std::shared_ptr<ControlTaskRunner> control;
  std::vector<PlayerEvent> events;
  std::function<void()> on_event;
  absl::Status result;
};

class DecodedEvents final : public DecoderCallbacks {
 public:
  absl::Status OnDecoded(DecoderEvent event) override {
    events.push_back(std::move(event));
    return absl::OkStatus();
  }
  std::vector<DecoderEvent> events;
};

class PortTest : public ::testing::Test {
 protected:
  PortTest()
      : control_(std::make_shared<RunnerAdapter>(
            environment_.CreateSequencedTaskRunner(
                umbra::base::TaskPriority::kControl))),
        port_(control_) {
    events_->control = control_;
  }

  void Run(ControlTaskRunner::Task task) {
    ASSERT_TRUE(control_->PostTask(std::move(task), absl::ZeroDuration()).ok());
    environment_.RunUntilIdle();
  }

  void Create(PlayerConfig config = {}) {
    if (config.streams.empty()) {
      config.streams = {Video(), Audio()};
    }
    dispatcher_ = std::make_shared<CallbackDispatcher>(config.initial_ticket,
                                                       control_, events_);
    auto result = port_.CreatePlayer(std::move(config), dispatcher_);
    ASSERT_TRUE(result.ok()) << result.status();
    player_ = std::move(*result);
    fake_ = dynamic_cast<FakePlayer*>(player_.get());
  }

  void TearDown() override {
    Run([this] {
      if (player_ != nullptr) {
        EXPECT_TRUE(player_->Stop().ok());
      }
      if (dispatcher_ != nullptr) {
        dispatcher_->Stop();
      }
      player_.reset();
    });
  }

  umbra::base::TestTaskEnvironment environment_;
  std::shared_ptr<ControlTaskRunner> control_;
  FakePort port_;
  std::shared_ptr<Events> events_ = std::make_shared<Events>();
  std::shared_ptr<CallbackDispatcher> dispatcher_;
  std::unique_ptr<Player> player_;
  FakePlayer* fake_ = nullptr;
};

TEST_F(PortTest, OneWritePerGrantAndZeroCopySampleLifetime) {
  Run([this] {
    Create();
    auto sample = Sample();
    const auto* address = sample.data.data();
    EXPECT_EQ(player_->WriteSamples(1, 7, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(fake_->RequestData(7, 2).ok());
    EXPECT_TRUE(player_->WriteSamples(1, 7, {&sample, 1}).ok());
    EXPECT_EQ(player_->WriteSamples(1, 7, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
    sample = {};
    ASSERT_EQ(fake_->Samples().size(), 1);
    EXPECT_EQ(fake_->Samples()[0].data.data(), address);
    EXPECT_EQ(fake_->Samples()[0].data.span()[0], 1);
    EXPECT_EQ(player_->GetSnapshot()->frames.decoded, 1);
  });
  ASSERT_EQ(events_->events.size(), 1);
  EXPECT_EQ(std::get<NeedsData>(events_->events[0].detail).max_samples, 2);
}

TEST_F(PortTest, InvalidWriteDoesNotConsumeGrant) {
  Run([this] {
    Create();
    const std::array samples{Sample(), Sample()};
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_EQ(player_->WriteSamples(1, 7, samples).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->WriteSamples(1, 7, {samples.data(), 1}).ok());
    EXPECT_EQ(fake_->RequestData(100, 1).code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_F(PortTest, UnsupportedConfigurationChangeAndEncryptionDoNotConsumeGrant) {
  Run([this] {
    Create();
    auto sample = Sample();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    auto change = std::make_shared<StreamInfo>(Video().info);
    change->codec = CodecId::kHevc;
    sample.config_change = change;
    EXPECT_EQ(player_->WriteSamples(1, 7, {&sample, 1}).code(),
              absl::StatusCode::kUnimplemented);
    change->codec = CodecId::kH264;
    change->codec_config = SharedBytes::Adopt({9, 8});
    sample.encryption = EncryptionInfo{};
    EXPECT_EQ(player_->WriteSamples(1, 7, {&sample, 1}).code(),
              absl::StatusCode::kInvalidArgument);
    sample.encryption.reset();
    EXPECT_TRUE(player_->WriteSamples(1, 7, {&sample, 1}).ok());
    EXPECT_EQ(fake_->Samples()[0].config_change->codec_config.data(),
              change->codec_config.data());
  });
}

TEST_F(PortTest, SeekDiscardsAlreadyPostedAndLatePlatformCallbacks) {
  Run([this] {
    Create();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_TRUE(dispatcher_->AdvanceTicket(2).ok());
    EXPECT_TRUE(player_->Seek(2, MediaTime::Zero()).ok());
    const auto sample = Sample();
    EXPECT_TRUE(player_->WriteSamples(1, 7, {&sample, 1}).ok());
    EXPECT_TRUE(fake_->Samples().empty());
    EXPECT_TRUE(dispatcher_->OnEvent({1, NeedsData{7, 1}}).ok());
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
  });
  ASSERT_EQ(events_->events.size(), 1);
  EXPECT_EQ(events_->events[0].ticket, 2);
}

TEST_F(PortTest, FlushInvalidatesGrantsAndRejectsNonIncreasingTickets) {
  Run([this] {
    Create();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_EQ(player_->Flush(1).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(dispatcher_->AdvanceTicket(2).ok());
    EXPECT_TRUE(player_->Flush(2).ok());
    const auto sample = Sample();
    EXPECT_EQ(player_->WriteSamples(2, 7, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(player_->WriteSamples(3, 7, {&sample, 1}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(dispatcher_->AdvanceTicket(2).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->Flush(std::numeric_limits<SeekTicket>::max()).ok());
    EXPECT_EQ(player_->Flush(0).code(), absl::StatusCode::kInvalidArgument);
  });
  EXPECT_TRUE(events_->events.empty());
}

TEST_F(PortTest, CallbackFromAnotherThreadIsDeliveredOnlyOnControlSequence) {
  Run([this] { Create(); });
  std::thread platform(
      [this] { EXPECT_TRUE(dispatcher_->OnEvent({1, NeedsData{7, 1}}).ok()); });
  platform.join();
  EXPECT_TRUE(events_->events.empty());
  environment_.RunUntilIdle();
  ASSERT_EQ(events_->events.size(), 1);
}

TEST_F(PortTest, FutureCallbackFromPlatformThreadIsRejectedWithoutDelivery) {
  Run([this] { Create(); });
  absl::Status result;
  std::thread platform([this, &result] {
    result = dispatcher_->OnEvent(
        {.ticket = 2, .detail = NeedsData{.track_id = 7, .max_samples = 1}});
  });
  platform.join();
  EXPECT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
  environment_.RunUntilIdle();
  EXPECT_TRUE(events_->events.empty());
  Run([this] {
    EXPECT_TRUE(dispatcher_->AdvanceTicket(2).ok());
    EXPECT_TRUE(player_->Flush(2).ok());
    EXPECT_TRUE(dispatcher_->OnEvent({1, NeedsData{7, 1}}).ok());
    EXPECT_TRUE(dispatcher_->OnEvent({2, NeedsData{7, 1}}).ok());
  });
  ASSERT_EQ(events_->events.size(), 1);
  EXPECT_EQ(events_->events[0].ticket, 2);
}

TEST_F(PortTest, StopSuppressesQueuedCallbacksAndIsTerminalAndIdempotent) {
  Run([this] {
    Create();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_TRUE(player_->Stop().ok());
    dispatcher_->Stop();
    EXPECT_TRUE(player_->Stop().ok());
    EXPECT_TRUE(dispatcher_->OnEvent({1, NeedsData{7, 1}}).ok());
    EXPECT_EQ(fake_->RequestData(7, 1).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(player_->GetSnapshot().status().code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(dispatcher_->AdvanceTicket(2).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  EXPECT_TRUE(events_->events.empty());
}

TEST_F(PortTest, EndOfStreamIsPerTrackAndSeekResetsIt) {
  Run([this] {
    Create();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(1, 7).ok());
    EXPECT_EQ(fake_->RequestData(7, 1).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(fake_->RequestData(8, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(1, 8).ok());
  });
  ASSERT_EQ(events_->events.size(), 5);
  EXPECT_EQ(std::get<StateChanged>(events_->events[1].detail).track_id, 7);
  EXPECT_EQ(std::get<StateChanged>(events_->events[3].detail).track_id, 8);
  EXPECT_FALSE(std::get<StateChanged>(events_->events[4].detail).track_id);
  Run([this] {
    EXPECT_TRUE(dispatcher_->AdvanceTicket(2).ok());
    EXPECT_TRUE(player_->Seek(2, MediaTime::Zero()).ok());
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(2, 7).ok());
  });
  ASSERT_EQ(events_->events.size(), 7);
  EXPECT_EQ(std::get<StateChanged>(events_->events[6].detail).track_id, 7);
  Run([this] {
    EXPECT_TRUE(fake_->RequestData(8, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(2, 8).ok());
    EXPECT_EQ(player_->EndOfStream(2, 8).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  ASSERT_EQ(events_->events.size(), 10);
  EXPECT_FALSE(std::get<StateChanged>(events_->events[9].detail).track_id);
  EXPECT_EQ(events_->events[9].ticket, 2);
  Run([this] {
    EXPECT_TRUE(dispatcher_->AdvanceTicket(3).ok());
    EXPECT_TRUE(player_->Flush(3).ok());
    EXPECT_TRUE(fake_->RequestData(8, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(3, 8).ok());
  });
  ASSERT_EQ(events_->events.size(), 12);
  EXPECT_EQ(std::get<StateChanged>(events_->events[11].detail).track_id, 8);
  Run([this] {
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    EXPECT_TRUE(player_->EndOfStream(3, 7).ok());
  });
  ASSERT_EQ(events_->events.size(), 15);
  EXPECT_FALSE(std::get<StateChanged>(events_->events[14].detail).track_id);
  EXPECT_EQ(events_->events[14].ticket, 3);
}

TEST_F(PortTest, FragmentsShareStorageAndConsumeOneSelectedTrackGrant) {
  Run([this] {
    PlayerConfig config;
    config.input_mode = InputMode::kFragments;
    Create(config);
    MediaFragment fragment{.data = SharedBytes::Adopt({1, 2, 3}),
                           .track_ids = {7, 8}};
    const auto* address = fragment.data.data();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
    const auto sample = Sample();
    EXPECT_EQ(player_->WriteSamples(1, 7, {&sample, 1}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->WriteFragment(1, 7, fragment).ok());
    fragment = {};
    ASSERT_EQ(fake_->Fragments().size(), 1);
    EXPECT_EQ(fake_->Fragments()[0].data.data(), address);
    EXPECT_EQ(player_->WriteFragment(1, 7, fake_->Fragments()[0]).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(PortTest, OutputModeRateVolumeAndMediaClock) {
  Run([this] {
    PlayerConfig config;
    auto video = Video();
    video.output_mode = OutputMode::kPunchOut;
    config.streams = {video};
    Create(config);
    EXPECT_TRUE(player_->SetBounds(7, {0, 0, 1920, 1080, 3}).ok());
    EXPECT_EQ(player_->SetBounds(7, {0, 0, -1, 1, 0}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(player_->GetCurrentFrame(7).status().code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->SetPlaybackRate(0).ok());
    EXPECT_TRUE(player_->SetPlaybackRate(2).ok());
    EXPECT_EQ(player_->SetPlaybackRate(-1).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(player_->SetPlaybackRate(std::numeric_limits<double>::quiet_NaN())
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->SetVolume(0.5).ok());
    EXPECT_EQ(player_->SetVolume(2).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(player_->Seek(2, MediaTime::Unknown()).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(player_->Seek(2, MediaTime::FromMicroseconds(500)).ok());
    EXPECT_EQ(player_->GetSnapshot()->media_time,
              MediaTime::FromMicroseconds(500));
  });
}

TEST_F(PortTest, InstanceLimitVersionAndCapabilities) {
  Run([this] {
    Create();
    PlayerConfig config;
    config.streams = {Video()};
    EXPECT_EQ(port_.CreatePlayer(config, dispatcher_).status().code(),
              absl::StatusCode::kResourceExhausted);
    config.api_version = kApiVersion + 1;
    EXPECT_EQ(port_.CreatePlayer(config, dispatcher_).status().code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(port_.GetLimits()->max_players, 1);
    CapabilityRequest request;
    request.stream = Video().info;
    EXPECT_TRUE(port_.QueryDecoding(request)->supported);
    request.stream.codec = CodecId::kUnknown;
    EXPECT_FALSE(port_.QueryDecoding(request)->supported);
    EXPECT_TRUE(*port_.CanChangeType(Video(), Video()));
    EXPECT_FALSE(*port_.CanChangeType(Video(), Audio()));
    EXPECT_TRUE(player_->Stop().ok());
    player_.reset();
    config.api_version = kApiVersion;
    EXPECT_TRUE(port_.CreatePlayer(config, dispatcher_).ok());
  });
}

TEST_F(PortTest, RecreateHintAndCanonicalStatusArePreserved) {
  Run([this] {
    Create();
    EXPECT_TRUE(fake_
                    ->ReportError({absl::UnavailableError("capability changed"),
                                   PortErrorCategory::kCapability,
                                   RecoveryHint::kRecreate})
                    .ok());
  });
  ASSERT_EQ(events_->events.size(), 1);
  const auto& error = std::get<PortError>(events_->events[0].detail);
  EXPECT_EQ(error.status.code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error.recovery, RecoveryHint::kRecreate);
}

TEST_F(PortTest, FailedDeliveryIsObservable) {
  events_->result = absl::CancelledError("consumer shut down");
  events_->result.SetPayload("test/payload", absl::Cord("preserved"));
  Run([this] {
    Create();
    EXPECT_TRUE(fake_->RequestData(7, 1).ok());
  });
  EXPECT_EQ(dispatcher_->GetDeliveryStatus(), events_->result);
  EXPECT_EQ(dispatcher_->GetDeliveryStatus().GetPayload("test/payload"),
            events_->result.GetPayload("test/payload"));
}

TEST_F(PortTest, MergedErrorDetailSurvivesPortAndDeliveryBoundaries) {
  const umbra::base::UrlRedactor redactor({1, 2});
  const auto error =
      umbra::base::MakeError(umbra::base::ErrorCode::kDecoderFallback, redactor,
                             {{"codec", "avc1.64001f"}});
  events_->result = error;
  Run([this, error] {
    Create();
    EXPECT_TRUE(fake_
                    ->ReportError({error, PortErrorCategory::kDecode,
                                   RecoveryHint::kRecreate})
                    .ok());
  });
  ASSERT_EQ(events_->events.size(), 1);
  const auto& delivered = std::get<PortError>(events_->events[0].detail);
  const auto detail = umbra::base::GetErrorDetail(delivered.status);
  ASSERT_TRUE(detail.ok()) << detail.status();
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ(detail->value().code, umbra::base::ErrorCode::kDecoderFallback);
  EXPECT_EQ(detail->value().context.at("codec"), "avc1.64001f");
  EXPECT_EQ(delivered.recovery, RecoveryHint::kRecreate);
  EXPECT_EQ(dispatcher_->GetDeliveryStatus(), error);
  EXPECT_EQ(dispatcher_->GetDeliveryStatus().GetPayload(
                umbra::base::kErrorDetailPayloadUrl),
            error.GetPayload(umbra::base::kErrorDetailPayloadUrl));
}

TEST_F(PortTest, ReentrantPortCallFromDeliveredCallbackIsDiagnosed) {
#ifndef NDEBUG
  EXPECT_DEATH(
      {
        Run([this] { Create(); });
        events_->on_event = [this] {
          auto status = player_->Stop();
          static_cast<void>(status);
        };
        Run([this] { EXPECT_TRUE(fake_->RequestData(7, 1).ok()); });
      },
      "CallbackScope::IsActive");
#else
  GTEST_SKIP() << "debug-only reentrancy diagnostic";
#endif
}

TEST_F(PortTest, DeferredPortCallFromCallbackIsAllowed) {
  Run([this] { Create(); });
  events_->on_event = [this] {
    EXPECT_TRUE(
        control_
            ->PostTask([this] { EXPECT_TRUE(player_->SetVolume(0.5).ok()); },
                       absl::ZeroDuration())
            .ok());
  };
  Run([this] { EXPECT_TRUE(fake_->RequestData(7, 1).ok()); });
  EXPECT_TRUE(dispatcher_->GetDeliveryStatus().ok());
}

TEST_F(PortTest, AudioAndVideoDecodersKeepPortOwnedOutputAlive) {
  Run([this] {
    auto events = std::make_shared<DecodedEvents>();
    auto video = port_.CreateVideoDecoder(Video(), 1, events);
    auto audio = port_.CreateAudioDecoder(Audio(), 1, events);
    ASSERT_TRUE(video.ok());
    ASSERT_TRUE(audio.ok());
    auto* fake_video = dynamic_cast<FakeDecoder*>(video->get());
    auto* fake_audio = dynamic_cast<FakeDecoder*>(audio->get());
    EXPECT_TRUE(fake_video->RequestData(1).ok());
    auto sample = Sample();
    EXPECT_TRUE((*video)->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_TRUE(fake_audio->RequestData(1).ok());
    sample.track_id = 8;
    EXPECT_TRUE((*audio)->WriteSamples(1, {&sample, 1}).ok());
    auto frame = std::get<DecodedVideoFrame>(events->events[1].detail);
    auto buffer = std::get<DecodedAudioBuffer>(events->events[3].detail);
    std::weak_ptr<const void> const video_owner = frame.storage;
    std::weak_ptr<const void> const audio_owner = buffer.storage;
    EXPECT_TRUE((*video)->Flush(2).ok());
    EXPECT_TRUE((*video)->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_TRUE(fake_video->RequestData(1).ok());
    EXPECT_TRUE((*video)->EndOfStream(2).ok());
    EXPECT_TRUE((*video)->Stop().ok());
    EXPECT_TRUE((*audio)->Stop().ok());
    video->reset();
    audio->reset();
    events->events.clear();
    EXPECT_FALSE(video_owner.expired());
    EXPECT_FALSE(audio_owner.expired());
    frame = {};
    buffer = {};
    EXPECT_TRUE(video_owner.expired());
    EXPECT_TRUE(audio_owner.expired());
  });
}

class Frames final : public VideoFrameSource {
 public:
  std::optional<DecodedVideoFrame> TakeDueFrame(
      MediaTime time) noexcept override {
    last_time = time;
    return std::exchange(frame, std::nullopt);
  }
  MediaTime last_time;
  std::optional<DecodedVideoFrame> frame;
};

class AudioBytes final : public AudioSource {
 public:
  size_t Read(std::span<uint8_t> destination) noexcept override {
    std::ranges::fill(destination, 0);
    return destination.size();
  }
};

class Pressure final : public MemoryPressureObserver {
 public:
  absl::Status OnMemoryPressure(MemoryPressure value) override {
    pressure = value;
    return absl::OkStatus();
  }
  MemoryPressure pressure = MemoryPressure::kModerate;
};

TEST_F(PortTest, SinksLoggingMemoryPressureAndRunnerHooks) {
  Run([this] {
    auto video = port_.CreateVideoSink(OutputMode::kTexture);
    auto audio = port_.CreateAudioSink();
    ASSERT_TRUE(video.ok());
    ASSERT_TRUE(audio.ok());
    auto frames = std::make_shared<Frames>();
    frames->frame = DecodedVideoFrame{};
    EXPECT_TRUE((*video)->Start(frames).ok());
    EXPECT_TRUE((*video)->SetBounds({0, 0, 640, 360, 0}).ok());
    auto* fake_video = dynamic_cast<FakeVideoSink*>(video->get());
    EXPECT_TRUE(fake_video->Vsync(MediaTime::Zero()).has_value());
    EXPECT_EQ(frames->last_time, MediaTime::Zero());
    auto source = std::make_shared<AudioBytes>();
    EXPECT_TRUE(
        (*audio)->Start(std::get<AudioInfo>(Audio().info.format), source).ok());
    std::array<uint8_t, 16> bytes{};
    EXPECT_EQ(dynamic_cast<FakeAudioSink*>(audio->get())->Render(bytes), 16);
    EXPECT_EQ(*(*audio)->GetOutputLatency(), TimeDelta::Zero());
    EXPECT_TRUE((*video)->Stop().ok());
    EXPECT_TRUE((*audio)->Stop().ok());
    EXPECT_EQ(dynamic_cast<FakeAudioSink*>(audio->get())->Render(bytes), 0);
    EXPECT_FALSE(fake_video->Vsync(MediaTime::Zero()).has_value());
    auto pressure = std::make_shared<Pressure>();
    EXPECT_TRUE(port_.SetMemoryPressureObserver(pressure).ok());
    EXPECT_TRUE(port_.SignalMemoryPressure(MemoryPressure::kCritical).ok());
    EXPECT_EQ(pressure->pressure, MemoryPressure::kCritical);
    EXPECT_TRUE(port_.SetMemoryPressureObserver(nullptr).ok());
    EXPECT_EQ(port_.SignalMemoryPressure(MemoryPressure::kCritical).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(port_.Log(LogLevel::kInfo, "redacted").ok());
    EXPECT_EQ(port_.Logs().back().message, "redacted");
    EXPECT_TRUE(port_.Log(LogLevel::kVerbose, "verbose").ok());
    EXPECT_EQ(port_.Logs().back().level, LogLevel::kVerbose);
    EXPECT_TRUE(port_
                    .CreateTaskRunner(TaskPriority::kControl,
                                      BlockingPolicy::kNonBlocking)
                    .ok());
    EXPECT_EQ(port_
                  .CreateTaskRunner(TaskPriority::kBackground,
                                    BlockingPolicy::kMayBlock)
                  .status()
                  .code(),
              absl::StatusCode::kUnimplemented);
  });
}

TEST_F(PortTest, PlatformLogAndPressureHooksCanArriveOnAnotherThread) {
  auto pressure = std::make_shared<Pressure>();
  Run([this, pressure] {
    EXPECT_TRUE(port_.SetMemoryPressureObserver(pressure).ok());
  });
  std::thread platform([this] {
    EXPECT_TRUE(
        port_.Log(LogLevel::kWarning, "redacted platform message").ok());
    EXPECT_TRUE(port_.SignalMemoryPressure(MemoryPressure::kCritical).ok());
  });
  platform.join();
  EXPECT_EQ(port_.Logs().back().message, "redacted platform message");
  EXPECT_EQ(pressure->pressure, MemoryPressure::kCritical);
  Run([this] { EXPECT_TRUE(port_.SetMemoryPressureObserver(nullptr).ok()); });
}

class ShortAudioSource final : public AudioSource {
 public:
  explicit ShortAudioSource(size_t bytes) : bytes_(bytes) {}
  size_t Read(std::span<uint8_t> destination) noexcept override {
    ++calls;
    std::ranges::fill(destination, 42);
    return bytes_;
  }
  size_t calls = 0;

 private:
  size_t bytes_;
};

TEST_F(PortTest, AudioShortReadAndUnderrunFillFloatSilence) {
  Run([] {
    FakeAudioSink sink;
    auto source = std::make_shared<ShortAudioSource>(12);
    AudioInfo const format{.sample_rate = 48000,
                           .channel_count = 3,
                           .channel_mask = 0,
                           .sample_format = SampleFormat::kF32};
    EXPECT_TRUE(sink.Start(format, source).ok());
    std::array<uint8_t, 24> bytes{};
    bytes.fill(99);
    EXPECT_EQ(sink.Render(bytes), 12);
    EXPECT_TRUE(std::ranges::all_of(std::span(bytes).first(12),
                                    [](uint8_t byte) { return byte == 42; }));
    EXPECT_TRUE(std::ranges::all_of(std::span(bytes).subspan(12),
                                    [](uint8_t byte) { return byte == 0; }));
    EXPECT_TRUE(sink.Stop().ok());
    bytes.fill(99);
    EXPECT_EQ(sink.Render(bytes), 0);
    EXPECT_TRUE(
        std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0; }));
    EXPECT_EQ(source->calls, 1);

    FakeAudioSink underrun;
    auto empty = std::make_shared<ShortAudioSource>(0);
    EXPECT_TRUE(underrun.Start(format, empty).ok());
    bytes.fill(99);
    EXPECT_EQ(underrun.Render(bytes), 0);
    EXPECT_TRUE(
        std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0; }));
    EXPECT_TRUE(underrun.Stop().ok());
  });
}

TEST_F(PortTest, UnsupportedAudioFormatsAndInvalidWholeFramesAreObservable) {
  Run([] {
    FakeAudioSink sink;
    auto source = std::make_shared<ShortAudioSource>(3);
    AudioInfo format{.sample_rate = 48000,
                     .channel_count = 2,
                     .channel_mask = 3,
                     .sample_format = SampleFormat::kU8};
    EXPECT_EQ(sink.Start(format, source).code(),
              absl::StatusCode::kUnimplemented);
    EXPECT_EQ(source->calls, 0);
    format.sample_format = SampleFormat::kF32;
    EXPECT_TRUE(sink.Start(format, source).ok());
    std::array<uint8_t, 16> bytes{};
    bytes.fill(99);
    EXPECT_EQ(sink.Render(bytes), 0);
    EXPECT_EQ(sink.GetOutputLatency().status().code(),
              absl::StatusCode::kDataLoss);
    EXPECT_TRUE(
        std::ranges::all_of(bytes, [](uint8_t byte) { return byte == 0; }));
    EXPECT_TRUE(sink.Stop().ok());

    FakeAudioSink partial;
    EXPECT_TRUE(partial.Start(format, source).ok());
    std::array<uint8_t, 7> partial_frame{};
    partial_frame.fill(99);
    EXPECT_EQ(partial.Render(partial_frame), 0);
    EXPECT_EQ(partial.GetOutputLatency().status().code(),
              absl::StatusCode::kDataLoss);
    EXPECT_EQ(source->calls, 1);
    EXPECT_TRUE(partial.Stop().ok());
  });
}

struct SuspendedCall {
  void Hold() noexcept {
    calls.fetch_add(1);
    entered.store(true, std::memory_order_release);
    while (!resume.load(std::memory_order_acquire)) {
    }
  }
  void WaitUntilEntered() const {
    while (!entered.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
  std::atomic<size_t> calls{0};
  std::atomic<bool> entered{false};
  std::atomic<bool> resume{false};
};

class SuspendedAudioSource final : public AudioSource {
 public:
  explicit SuspendedAudioSource(SuspendedCall& call) : call_(&call) {}
  size_t Read(std::span<uint8_t> destination) noexcept override {
    call_->Hold();
    std::ranges::fill(destination, 0);
    return destination.size();
  }

 private:
  SuspendedCall* call_;
};

class SuspendedFrameSource final : public VideoFrameSource {
 public:
  explicit SuspendedFrameSource(SuspendedCall& call) : call_(&call) {}
  std::optional<DecodedVideoFrame> TakeDueFrame(
      [[maybe_unused]] MediaTime time) noexcept override {
    call_->Hold();
    return std::nullopt;
  }

 private:
  SuspendedCall* call_;
};

TEST_F(PortTest, AudioStopWaitsForInFlightSourceWithoutBlockingOrReleasingIt) {
  FakeAudioSink sink;
  SuspendedCall call;
  auto source = std::make_shared<SuspendedAudioSource>(call);
  std::weak_ptr<AudioSource> owner = source;
  Run([&] {
    EXPECT_TRUE(
        sink.Start(std::get<AudioInfo>(Audio().info.format), source).ok());
  });
  source.reset();
  std::array<uint8_t, 16> bytes{};
  size_t read = 0;
  std::thread platform([&] { read = sink.Render(bytes); });
  call.WaitUntilEntered();
  Run([&] {
    EXPECT_EQ(sink.Stop().code(), absl::StatusCode::kUnavailable);
    EXPECT_FALSE(owner.expired());
    std::array<uint8_t, 16> later{};
    later.fill(99);
    EXPECT_EQ(sink.Render(later), 0);
    EXPECT_TRUE(
        std::ranges::all_of(later, [](uint8_t byte) { return byte == 0; }));
    EXPECT_EQ(sink.Stop().code(), absl::StatusCode::kUnavailable);
  });
  call.resume.store(true, std::memory_order_release);
  platform.join();
  EXPECT_EQ(read, bytes.size());
  Run([&] {
    EXPECT_TRUE(sink.Stop().ok());
    EXPECT_TRUE(sink.Stop().ok());
    EXPECT_TRUE(owner.expired());
  });
  std::thread after_stop([&] { EXPECT_EQ(sink.Render(bytes), 0); });
  after_stop.join();
  EXPECT_EQ(call.calls.load(), 1);
}

TEST_F(PortTest, VideoStopWaitsForInFlightSourceWithoutBlockingOrReleasingIt) {
  FakeVideoSink sink;
  SuspendedCall call;
  auto source = std::make_shared<SuspendedFrameSource>(call);
  std::weak_ptr<VideoFrameSource> owner = source;
  Run([&] { EXPECT_TRUE(sink.Start(source).ok()); });
  source.reset();
  std::thread platform(
      [&] { EXPECT_FALSE(sink.Vsync(MediaTime::Zero()).has_value()); });
  call.WaitUntilEntered();
  Run([&] {
    EXPECT_EQ(sink.Stop().code(), absl::StatusCode::kUnavailable);
    EXPECT_FALSE(owner.expired());
    EXPECT_FALSE(sink.Vsync(MediaTime::Zero()).has_value());
    EXPECT_EQ(sink.Stop().code(), absl::StatusCode::kUnavailable);
  });
  call.resume.store(true, std::memory_order_release);
  platform.join();
  Run([&] {
    EXPECT_TRUE(sink.Stop().ok());
    EXPECT_TRUE(sink.Stop().ok());
    EXPECT_TRUE(owner.expired());
  });
  std::thread after_stop(
      [&] { EXPECT_FALSE(sink.Vsync(MediaTime::Zero()).has_value()); });
  after_stop.join();
  EXPECT_EQ(call.calls.load(), 1);
}

TEST_F(PortTest, RunnerUsesSharedVirtualClockAndReportsRejection) {
  bool ran = false;
  EXPECT_TRUE(
      control_->PostTask([&ran] { ran = true; }, absl::Milliseconds(10)).ok());
  environment_.FastForwardBy(absl::Milliseconds(9));
  EXPECT_FALSE(ran);
  environment_.FastForwardBy(absl::Milliseconds(1));
  EXPECT_TRUE(ran);
  EXPECT_EQ(control_->PostTask([] {}, -absl::Milliseconds(1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(control_->PostTask([] {}, absl::InfiniteDuration()).code(),
            absl::StatusCode::kInvalidArgument);
  umbra::base::TestTaskEnvironment local;
  RunnerAdapter stopped(
      local.CreateSequencedTaskRunner(umbra::base::TaskPriority::kControl));
  local.executor().Shutdown();
  EXPECT_EQ(stopped.PostTask([] {}, absl::ZeroDuration()).code(),
            absl::StatusCode::kUnavailable);
}

}
}
