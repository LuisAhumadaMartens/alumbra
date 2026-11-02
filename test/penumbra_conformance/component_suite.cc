#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

constexpr size_t kFrameBytes = 2 * sizeof(float);
constexpr size_t kRenderBytes = 64;
constexpr uint8_t kSourceByte = 0x7f;
constexpr uint8_t kPoisonByte = 0xab;

class DueFrameSource final : public VideoFrameSource {
 public:
  std::optional<DecodedVideoFrame> TakeDueFrame(
      MediaTime time) noexcept override {
    calls_.fetch_add(1);
    last_micros_.store(time.InMicroseconds());
    if (time < due_) {
      return std::nullopt;
    }
    DecodedVideoFrame frame;
    frame.presentation_time = due_;
    return frame;
  }
  [[nodiscard]] int Calls() const { return calls_.load(); }
  [[nodiscard]] int64_t LastMicros() const { return last_micros_.load(); }
  [[nodiscard]] MediaTime Due() const { return due_; }

 private:
  const MediaTime due_ = MediaTime::FromMilliseconds(10);
  std::atomic<int> calls_{0};
  std::atomic<int64_t> last_micros_{0};
};

class ShortAudioSource final : public AudioSource {
 public:
  size_t Read(std::span<uint8_t> destination) noexcept override {
    reads_.fetch_add(1);
    const size_t bytes = std::min(destination.size(), kFrameBytes);
    std::fill_n(destination.begin(), bytes, kSourceByte);
    return bytes;
  }
  [[nodiscard]] int Reads() const { return reads_.load(); }

 private:
  std::atomic<int> reads_{0};
};

AudioInfo StereoFloat() {
  return AudioInfo{.sample_rate = 48000,
                   .channel_count = 2,
                   .channel_mask = 3,
                   .sample_format = SampleFormat::kF32};
}

}

TEST_P(ComponentConformance, VideoSinkFollowsItsLifecycle) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  Run([this] {
    auto sink = Harness().UnderTest().CreateVideoSink(OutputMode::kTexture);
    ASSERT_TRUE(sink.ok()) << sink.status();
    EXPECT_EQ((*sink)->Start(nullptr).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE((*sink)->Start(std::make_shared<DueFrameSource>()).ok());
    EXPECT_EQ((*sink)->Start(std::make_shared<DueFrameSource>()).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ((*sink)->SetBounds({.width = -1}).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE((*sink)->Stop().ok());
    EXPECT_TRUE((*sink)->Stop().ok());
    EXPECT_EQ((*sink)->Start(std::make_shared<DueFrameSource>()).code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_P(ComponentConformance, VideoSinkPullsFramesOnlyWhenDue) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  ALUMBRA_PORT_REQUIRE(Capability::kVsyncDrive);
  auto source = std::make_shared<DueFrameSource>();
  Run([&] {
    auto sink = Harness().UnderTest().CreateVideoSink(OutputMode::kTexture);
    ASSERT_TRUE(sink.ok()) << sink.status();
    ASSERT_TRUE((*sink)->Start(source).ok());
    const auto early = MediaTime::FromMilliseconds(5);
    EXPECT_FALSE(Harness().DriveVsync(**sink, early).has_value());
    EXPECT_EQ(source->LastMicros(), early.InMicroseconds());
    const auto late = MediaTime::FromMilliseconds(15);
    const auto frame = Harness().DriveVsync(**sink, late);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->presentation_time, source->Due());
    EXPECT_EQ(source->LastMicros(), late.InMicroseconds());
    EXPECT_TRUE((*sink)->Stop().ok());
    const int calls = source->Calls();
    EXPECT_FALSE(Harness().DriveVsync(**sink, late).has_value());
    EXPECT_EQ(source->Calls(), calls);
  });
}

TEST_P(ComponentConformance, AudioSinkFollowsItsLifecycle) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  Run([this] {
    auto sink = Harness().UnderTest().CreateAudioSink();
    ASSERT_TRUE(sink.ok()) << sink.status();
    auto source = std::make_shared<ShortAudioSource>();
    EXPECT_EQ((*sink)->Start(StereoFloat(), nullptr).code(),
              absl::StatusCode::kInvalidArgument);
    auto silent = StereoFloat();
    silent.sample_rate = 0;
    EXPECT_EQ((*sink)->Start(silent, source).code(),
              absl::StatusCode::kInvalidArgument);
    auto mono_less = StereoFloat();
    mono_less.channel_count = 0;
    EXPECT_EQ((*sink)->Start(mono_less, source).code(),
              absl::StatusCode::kInvalidArgument);
    auto unsupported = StereoFloat();
    unsupported.sample_format = SampleFormat::kF32Planar;
    const auto planar = (*sink)->Start(unsupported, source);
    EXPECT_TRUE(planar.code() == absl::StatusCode::kUnimplemented ||
                planar.code() == absl::StatusCode::kInvalidArgument)
        << planar;
    EXPECT_TRUE((*sink)->Start(StereoFloat(), source).ok());
    EXPECT_EQ((*sink)->Start(StereoFloat(), source).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE((*sink)->Stop().ok());
    EXPECT_TRUE((*sink)->Stop().ok());
  });
}

TEST_P(ComponentConformance, AudioLatencyIsUnknownOrNonNegative) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  Run([this] {
    auto sink = Harness().UnderTest().CreateAudioSink();
    ASSERT_TRUE(sink.ok()) << sink.status();
    ASSERT_TRUE((*sink)
                    ->Start(StereoFloat(), std::make_shared<ShortAudioSource>())
                    .ok());
    const auto latency = (*sink)->GetOutputLatency();
    ASSERT_TRUE(latency.ok()) << latency.status();
    EXPECT_TRUE(latency->is_unknown() || *latency >= TimeDelta::Zero());
    EXPECT_TRUE((*sink)->Stop().ok());
  });
}

TEST_P(ComponentConformance, AudioShortfallIsFilledWithSilenceUntilStop) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  ALUMBRA_PORT_REQUIRE(Capability::kAudioRenderDrive);
  auto source = std::make_shared<ShortAudioSource>();
  Run([&] {
    auto sink = Harness().UnderTest().CreateAudioSink();
    ASSERT_TRUE(sink.ok()) << sink.status();
    ASSERT_TRUE((*sink)->Start(StereoFloat(), source).ok());
    std::array<uint8_t, kRenderBytes> buffer{};
    buffer.fill(kPoisonByte);
    const size_t read = Harness().DriveAudioRender(**sink, buffer);
    ASSERT_LE(read, buffer.size());
    EXPECT_EQ(read % kFrameBytes, 0U);
    EXPECT_GT(read, 0U);
    for (size_t i = 0; i < buffer.size(); ++i) {
      EXPECT_EQ(buffer.at(i), i < read ? kSourceByte : 0) << "byte " << i;
    }
    EXPECT_TRUE((*sink)->Stop().ok());
    const int reads = source->Reads();
    buffer.fill(kPoisonByte);
    EXPECT_EQ(Harness().DriveAudioRender(**sink, buffer), 0U);
    EXPECT_EQ(source->Reads(), reads);
    EXPECT_TRUE(
        std::ranges::all_of(buffer, [](uint8_t byte) { return byte == 0; }));
  });
}

TEST_P(ComponentConformance, DecodersRejectInvalidConfiguration) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  Run([this] {
    auto receiver = std::make_shared<RecordingDecoderReceiver>();
    EXPECT_EQ(Harness()
                  .UnderTest()
                  .CreateVideoDecoder(AudioStream(), 1, receiver)
                  .status()
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(Harness()
                  .UnderTest()
                  .CreateAudioDecoder(VideoStream(), 1, receiver)
                  .status()
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(Harness()
                  .UnderTest()
                  .CreateVideoDecoder(VideoStream(), 0, receiver)
                  .status()
                  .code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(Harness()
                  .UnderTest()
                  .CreateVideoDecoder(VideoStream(), 1, nullptr)
                  .status()
                  .code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_P(ComponentConformance, DecoderFollowsGrantsAndKeepsOutputAlive) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  auto receiver = std::make_shared<RecordingDecoderReceiver>();
  std::unique_ptr<Decoder> decoder;
  const auto sample = MakeSample(kVideoTrack, 0);
  Run([&] {
    auto result =
        Harness().UnderTest().CreateVideoDecoder(VideoStream(), 1, receiver);
    ASSERT_TRUE(result.ok()) << result.status();
    decoder = std::move(*result);
    EXPECT_EQ(decoder->WriteSamples(1, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(decoder->EndOfStream(1).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(Harness().RequestDecoderData(*decoder, 1).ok());
  });
  size_t granted = 0;
  ASSERT_TRUE(receiver->ConsumeGrant(1, &granted));
  EXPECT_GT(granted, 0U);
  Run([&] {
    EXPECT_TRUE(decoder->WriteSamples(1, {&sample, 1}).ok());
    EXPECT_EQ(decoder->WriteSamples(1, {&sample, 1}).code(),
              absl::StatusCode::kFailedPrecondition);
  });
  std::shared_ptr<const void> storage;
  for (const auto& event : receiver->Events()) {
    if (const auto* frame = std::get_if<DecodedVideoFrame>(&event.detail)) {
      storage = frame->storage;
    }
  }
  ASSERT_NE(storage, nullptr);
  Run([&] {
    EXPECT_TRUE(decoder->Stop().ok());
    EXPECT_TRUE(decoder->Stop().ok());
    decoder.reset();
  });
  EXPECT_NE(storage, nullptr);
  EXPECT_EQ(receiver->OutsideCallbackScope(), 0U);
}

TEST_P(ComponentConformance, DecoderFlushRequiresAnIncreasingTicket) {
  ALUMBRA_PORT_REQUIRE(Capability::kComponents);
  auto receiver = std::make_shared<RecordingDecoderReceiver>();
  Run([&] {
    auto result =
        Harness().UnderTest().CreateVideoDecoder(VideoStream(), 1, receiver);
    ASSERT_TRUE(result.ok()) << result.status();
    auto decoder = std::move(*result);
    EXPECT_EQ(decoder->Flush(1).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(decoder->Flush(0).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(decoder->Flush(2).ok());
    EXPECT_EQ(decoder->Flush(2).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(decoder->Stop().ok());
  });
}

}
