#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/control_runner_adapter.h"
#include "penumbra_conformance/port_harness.h"
#include "umbra/base/task_runner.h"

namespace penumbra::conformance {

std::string CapabilityName(Capability capability) {
  switch (capability) {
    case Capability::kDataRequestHook:
      return "data-request-hook";
    case Capability::kErrorInjection:
      return "error-injection";
    case Capability::kClockAdvance:
      return "clock-advance";
    case Capability::kFragmentInput:
      return "fragment-input";
    case Capability::kPunchOutOutput:
      return "punch-out-output";
    case Capability::kTextureOutput:
      return "texture-output";
    case Capability::kComponents:
      return "components";
    case Capability::kVsyncDrive:
      return "vsync-drive";
    case Capability::kAudioRenderDrive:
      return "audio-render-drive";
  }
  return "unknown";
}

void PrintTo(const PortCase& port_case, std::ostream* out) {
  *out << port_case.name;
}

std::string PortCaseName(const ::testing::TestParamInfo<PortCase>& info) {
  return info.param.name;
}

absl::Status PortHarness::RequestData([[maybe_unused]] Player& player,
                                      [[maybe_unused]] uint32_t track_id,
                                      [[maybe_unused]] size_t max_samples) {
  return absl::UnimplementedError("port has no data request hook");
}

absl::Status PortHarness::RequestDecoderData([[maybe_unused]] Decoder& decoder,
                                             [[maybe_unused]] size_t count) {
  return absl::UnimplementedError("port has no decoder data request hook");
}

absl::Status PortHarness::InjectError([[maybe_unused]] Player& player,
                                      [[maybe_unused]] const PortError& error) {
  return absl::UnimplementedError("port has no error injection hook");
}

absl::Status PortHarness::AdvancePlayback(
    [[maybe_unused]] Player& player, [[maybe_unused]] absl::Duration elapsed) {
  return absl::UnimplementedError("port has no clock advance hook");
}

std::optional<DecodedVideoFrame> PortHarness::DriveVsync(
    [[maybe_unused]] VideoSink& sink, [[maybe_unused]] MediaTime time) {
  return std::nullopt;
}

size_t PortHarness::DriveAudioRender(
    [[maybe_unused]] AudioSink& sink,
    [[maybe_unused]] std::span<uint8_t> destination) {
  return 0;
}

StreamConfig VideoStream(uint32_t track) {
  StreamConfig config;
  config.info.track_id = track;
  config.info.codec = CodecId::kH264;
  config.info.codec_string = "avc1.64001e";
  config.info.time_base = Rational::Of(1, 90000);
  VideoInfo video;
  video.coded_width = video.display_width = 640;
  video.coded_height = video.display_height = 360;
  config.info.format = video;
  return config;
}

StreamConfig AudioStream(uint32_t track) {
  StreamConfig config = VideoStream(track);
  config.info.codec = CodecId::kAac;
  config.info.codec_string = "mp4a.40.2";
  config.info.time_base = Rational::Of(1, 48000);
  config.info.format = AudioInfo{.sample_rate = 48000,
                                 .channel_count = 2,
                                 .channel_mask = 3,
                                 .sample_format = SampleFormat::kF32};
  return config;
}

PlayerConfig DefaultPlayerConfig() {
  PlayerConfig config;
  config.streams = {VideoStream(), AudioStream()};
  return config;
}

EncodedSample MakeSample(uint32_t track, int64_t index) {
  const bool video = track == kVideoTrack;
  EncodedSample sample;
  sample.track_id = track;
  sample.data = SharedBytes::Adopt({1, 2, 3, 4});
  sample.pts = {
      .value = index * (video ? 3000 : 1024),
      .time_base = video ? Rational::Of(1, 90000) : Rational::Of(1, 48000)};
  sample.key_frame = index == 0;
  return sample;
}

RecordingReceiver::RecordingReceiver(std::shared_ptr<ControlTaskRunner> control)
    : control_(std::move(control)) {}

absl::Status RecordingReceiver::OnEvent(PlayerEvent event) {
  const bool in_scope = CallbackScope::IsActive();
  const bool on_control = control_->RunsTasksInCurrentSequence();
  std::scoped_lock const lock(mutex_);
  outside_scope_ += in_scope ? 0 : 1;
  off_control_ += on_control ? 0 : 1;
  events_.push_back(std::move(event));
  consumed_.push_back(false);
  return absl::OkStatus();
}

std::vector<PlayerEvent> RecordingReceiver::Events() const {
  std::scoped_lock const lock(mutex_);
  return events_;
}

size_t RecordingReceiver::OutsideCallbackScope() const {
  std::scoped_lock const lock(mutex_);
  return outside_scope_;
}

size_t RecordingReceiver::OffControlSequence() const {
  std::scoped_lock const lock(mutex_);
  return off_control_;
}

bool RecordingReceiver::ConsumeGrant(uint32_t track_id, SeekTicket ticket,
                                     size_t* max_samples) {
  std::scoped_lock const lock(mutex_);
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto* needs = std::get_if<NeedsData>(&events_[i].detail);
    if (!consumed_[i] && needs != nullptr && needs->track_id == track_id &&
        events_[i].ticket == ticket) {
      consumed_[i] = true;
      *max_samples = needs->max_samples;
      return true;
    }
  }
  return false;
}

absl::Status RecordingDecoderReceiver::OnDecoded(DecoderEvent event) {
  const bool in_scope = CallbackScope::IsActive();
  std::scoped_lock const lock(mutex_);
  outside_scope_ += in_scope ? 0 : 1;
  events_.push_back(std::move(event));
  consumed_.push_back(false);
  return absl::OkStatus();
}

std::vector<DecoderEvent> RecordingDecoderReceiver::Events() const {
  std::scoped_lock const lock(mutex_);
  return events_;
}

size_t RecordingDecoderReceiver::OutsideCallbackScope() const {
  std::scoped_lock const lock(mutex_);
  return outside_scope_;
}

bool RecordingDecoderReceiver::ConsumeGrant(SeekTicket ticket,
                                            size_t* max_samples) {
  std::scoped_lock const lock(mutex_);
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto* needs = std::get_if<NeedsData>(&events_[i].detail);
    if (!consumed_[i] && needs != nullptr && events_[i].ticket == ticket) {
      consumed_[i] = true;
      *max_samples = needs->max_samples;
      return true;
    }
  }
  return false;
}

void PortConformanceTest::SetUp() {
  control_ = std::make_shared<ControlRunnerAdapter>(
      environment_.CreateSequencedTaskRunner(
          umbra::base::TaskPriority::kControl));
  receiver_ = std::make_shared<RecordingReceiver>(control_);
  harness_ = GetParam().factory(control_);
  ASSERT_NE(harness_, nullptr);
}

void PortConformanceTest::TearDown() {
  Run([this] {
    for (auto& extra : extra_players_) {
      EXPECT_TRUE(extra->Stop().ok());
    }
    for (auto& extra : extra_dispatchers_) {
      extra->Stop();
    }
    extra_players_.clear();
    if (player_ != nullptr) {
      EXPECT_TRUE(player_->Stop().ok());
    }
    if (dispatcher_ != nullptr) {
      dispatcher_->Stop();
    }
    player_.reset();
  });
}

bool PortConformanceTest::Has(Capability capability) {
  return GetParam().capabilities.contains(capability);
}

void PortConformanceTest::Run(ControlTaskRunner::Task task) {
  ASSERT_TRUE(control_->PostTask(std::move(task), absl::ZeroDuration()).ok());
  environment_.RunUntilIdle();
}

void PortConformanceTest::Settle() { environment_.RunUntilIdle(); }

absl::Status PortConformanceTest::Create(PlayerConfig config) {
  if (config.streams.empty()) {
    config.streams = {VideoStream(), AudioStream()};
  }
  ticket_ = config.initial_ticket;
  dispatcher_ = std::make_shared<CallbackDispatcher>(config.initial_ticket,
                                                     control_, receiver_);
  absl::Status status;
  Run([&] {
    auto result =
        harness_->UnderTest().CreatePlayer(std::move(config), dispatcher_);
    if (result.ok()) {
      player_ = std::move(*result);
    } else {
      status = result.status();
    }
  });
  return status;
}

absl::Status PortConformanceTest::CreateExtra(PlayerConfig config) {
  if (config.streams.empty()) {
    config.streams = {VideoStream(), AudioStream()};
  }
  auto dispatcher = std::make_shared<CallbackDispatcher>(
      config.initial_ticket, control_,
      std::make_shared<RecordingReceiver>(control_));
  absl::Status status;
  Run([&] {
    auto result =
        harness_->UnderTest().CreatePlayer(std::move(config), dispatcher);
    if (result.ok()) {
      extra_players_.push_back(std::move(*result));
      extra_dispatchers_.push_back(dispatcher);
    } else {
      status = result.status();
    }
  });
  return status;
}

absl::Status PortConformanceTest::TryCreate(
    PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks) {
  absl::Status status;
  Run([&] {
    auto result = harness_->UnderTest().CreatePlayer(std::move(config),
                                                     std::move(callbacks));
    if (result.ok()) {
      EXPECT_TRUE((*result)->Stop().ok());
    } else {
      status = result.status();
    }
  });
  return status;
}

bool PortConformanceTest::Grant(uint32_t track_id, size_t max_samples) {
  if (Has(Capability::kDataRequestHook)) {
    absl::Status status;
    Run([&] {
      status = harness_->RequestData(*player_, track_id, max_samples);
    });
    if (!status.ok()) {
      ADD_FAILURE() << "data request hook failed: " << status;
      return false;
    }
  }
  Settle();
  size_t granted = 0;
  return receiver_->ConsumeGrant(track_id, ticket_, &granted) && granted > 0;
}

absl::Status PortConformanceTest::DoSeek(SeekTicket ticket,
                                         MediaTime position) {
  absl::Status status;
  Run([&] {
    status = dispatcher_->AdvanceTicket(ticket);
    if (status.ok()) {
      status = player_->Seek(ticket, position);
    }
  });
  if (status.ok()) {
    ticket_ = ticket;
  }
  return status;
}

absl::Status PortConformanceTest::DoFlush(SeekTicket ticket) {
  absl::Status status;
  Run([&] {
    status = dispatcher_->AdvanceTicket(ticket);
    if (status.ok()) {
      status = player_->Flush(ticket);
    }
  });
  if (status.ok()) {
    ticket_ = ticket;
  }
  return status;
}

void PortConformanceTest::DestroyPlayer() {
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

}
