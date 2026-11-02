#include "penumbra/desktop/audio/audio_renderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "audio_test_support.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

using testing::ManualClock;
using testing::ScriptedSource;

constexpr int32_t kRate = 48000;
constexpr size_t kRampFrames = 240;
constexpr size_t kLastFrame = 999;
constexpr size_t kFlushFrames = 480;

class AudioRendererTest : public ::testing::Test {
 protected:
  void Build(int32_t channels = 2, SampleFormat format = SampleFormat::kF32) {
    channels_ = static_cast<size_t>(channels);
    source_ = std::make_shared<ScriptedSource>(
        channels, format == SampleFormat::kS16 ? 2 : 4);
    AudioRendererConfig config;
    config.sample_rate = kRate;
    config.channel_count = channels;
    config.sample_format = format;
    absl::StatusOr<std::unique_ptr<AudioRenderer>> renderer =
        AudioRenderer::Create(config, source_, &clock_);
    ASSERT_TRUE(renderer.ok()) << renderer.status();
    renderer_ = *std::move(renderer);
  }

  std::vector<float> Render(size_t frames) {
    std::vector<float> output(frames * channels_, -1.0F);
    renderer_->Render(output);
    return output;
  }

  size_t channels_ = 2;
  ManualClock clock_;
  std::shared_ptr<ScriptedSource> source_;
  std::unique_ptr<AudioRenderer> renderer_;
};

TEST_F(AudioRendererTest, CreateRejectsInvalidAndUnsupportedFormats) {
  auto source = std::make_shared<ScriptedSource>(2, 4);
  AudioRendererConfig config;
  config.sample_rate = kRate;
  config.channel_count = 2;
  EXPECT_EQ(AudioRenderer::Create(config, nullptr, &clock_).status().code(),
            absl::StatusCode::kInvalidArgument);
  AudioRendererConfig bad = config;
  bad.sample_rate = 0;
  EXPECT_EQ(AudioRenderer::Create(bad, source, &clock_).status().code(),
            absl::StatusCode::kInvalidArgument);
  bad = config;
  bad.channel_count = 0;
  EXPECT_EQ(AudioRenderer::Create(bad, source, &clock_).status().code(),
            absl::StatusCode::kInvalidArgument);
  bad = config;
  bad.sample_format = SampleFormat::kUnknown;
  EXPECT_EQ(AudioRenderer::Create(bad, source, &clock_).status().code(),
            absl::StatusCode::kInvalidArgument);
  for (const SampleFormat format :
       {SampleFormat::kU8, SampleFormat::kF32Planar, SampleFormat::kS16Planar,
        SampleFormat::kS32Planar, SampleFormat::kU8Planar}) {
    bad = config;
    bad.sample_format = format;
    EXPECT_EQ(AudioRenderer::Create(bad, source, &clock_).status().code(),
              absl::StatusCode::kUnimplemented);
  }
}

TEST_F(AudioRendererTest, FadesInThenPassesSamplesThrough) {
  Build();
  source_->SetConstant(0.5F);
  const std::vector<float> output = Render(1000);
  EXPECT_LT(output[0], 0.01F);
  for (size_t frame = 1; frame < kRampFrames; ++frame) {
    EXPECT_GE(output[frame * 2], output[(frame - 1) * 2]);
  }
  for (size_t frame = kRampFrames + 1; frame < 1000; ++frame) {
    EXPECT_FLOAT_EQ(output[frame * 2], 0.5F);
    EXPECT_FLOAT_EQ(output[(frame * 2) + 1], 0.5F);
  }
  EXPECT_EQ(renderer_->MediaFramesDelivered(), 1000);
}

TEST_F(AudioRendererTest, ConvertsSixteenBitSamples) {
  Build(1, SampleFormat::kS16);
  source_->SetConstant(0.5F);
  const std::vector<float> output = Render(1000);
  EXPECT_NEAR(output[999], 0.5F, 1e-3F);
}

TEST_F(AudioRendererTest, ShortReadsAreSilenceAndDoNotAdvanceMediaTime) {
  Build();
  source_->SetFrameLimit(300);
  const std::vector<float> output = Render(1000);
  EXPECT_EQ(renderer_->MediaFramesDelivered(), 300);
  EXPECT_EQ(renderer_->UnderrunFrames(), 700);
  for (size_t frame = 300; frame < 1000; ++frame) {
    EXPECT_EQ(output[frame * 2], 0.0F);
  }
}

// Emits a continuous stream of float samples 1, 2, 3, ... but always returns
// three bytes fewer than requested, so reads never end on a frame boundary.
class UnalignedSource final : public AudioSource {
 public:
  size_t Read(std::span<uint8_t> destination) noexcept override {
    const size_t count = destination.size() > 3 ? destination.size() - 3 : 0;
    for (size_t index = 0; index < count; ++index) {
      if (offset_ % sizeof(float) == 0) {
        value_ += 1.0F;
      }
      std::memcpy(
          &destination[index],
          reinterpret_cast<const uint8_t*>(&value_) + (offset_ % sizeof(float)),
          1);
      ++offset_;
    }
    return count;
  }

 private:
  float value_ = 0.0F;
  size_t offset_ = 0;
};

TEST_F(AudioRendererTest, PartialFramesCarryOverWithoutLosingSourceBytes) {
  AudioRendererConfig config;
  config.sample_rate = kRate;
  config.channel_count = 2;
  config.sample_format = SampleFormat::kF32;
  config.ramp_milliseconds = 0;
  auto source = std::make_shared<UnalignedSource>();
  absl::StatusOr<std::unique_ptr<AudioRenderer>> renderer =
      AudioRenderer::Create(config, source, &clock_);
  ASSERT_TRUE(renderer.ok()) << renderer.status();
  float expected = 1.0F;
  for (int call = 0; call < 8; ++call) {
    std::vector<float> output(1000 * 2, 0.0F);
    (*renderer)->Render(output);
    for (const float sample : output) {
      if (sample != 0.0F) {
        EXPECT_EQ(sample, expected);
        expected += 1.0F;
      }
    }
  }
  EXPECT_GT(expected, 100.0F);
}

TEST_F(AudioRendererTest, PauseRampsToSilenceAndStopsReadingTheSource) {
  Build();
  Render(2000);
  const int64_t before = source_->FramesRead();
  renderer_->Pause();
  const std::vector<float> ramp = Render(1000);
  for (size_t frame = 1; frame < 1000; ++frame) {
    EXPECT_LE(std::fabs(ramp[frame * 2] - ramp[(frame - 1) * 2]),
              (0.5F / static_cast<float>(kRampFrames)) + 1e-6F);
  }
  EXPECT_EQ(ramp[kLastFrame * 2], 0.0F);
  const int64_t after_ramp = source_->FramesRead();
  EXPECT_EQ(after_ramp - before, static_cast<int64_t>(kRampFrames));
  Render(1000);
  EXPECT_EQ(source_->FramesRead(), after_ramp);
  EXPECT_EQ(renderer_->UnderrunFrames(), 0);
  EXPECT_TRUE(renderer_->IsPaused());

  renderer_->Resume();
  const std::vector<float> resumed = Render(1000);
  EXPECT_LT(resumed[0], 0.01F);
  EXPECT_FLOAT_EQ(resumed[kLastFrame * 2], 0.5F);
  EXPECT_EQ(source_->FramesRead(), after_ramp + 1000);
}

TEST_F(AudioRendererTest, PauseAndResumeKeepTheSourceContiguous) {
  Build(1);
  source_->SetMode(ScriptedSource::Mode::kCounter);
  Render(1000);
  renderer_->Pause();
  Render(1000);
  renderer_->Resume();
  Render(1000);
  EXPECT_EQ(renderer_->MediaFramesDelivered(), source_->FramesRead());
  EXPECT_EQ(source_->FramesRead(),
            1000 + static_cast<int64_t>(kRampFrames) + 1000);
}

TEST_F(AudioRendererTest, VolumeAndMuteChangesAreRamped) {
  Build(1);
  source_->SetConstant(1.0F);
  Render(2000);
  renderer_->SetMuted(true);
  const std::vector<float> muted = Render(1000);
  for (size_t frame = 1; frame < 1000; ++frame) {
    EXPECT_LE(std::fabs(muted[frame] - muted[frame - 1]),
              (1.0F / static_cast<float>(kRampFrames)) + 1e-6F);
  }
  EXPECT_EQ(muted[999], 0.0F);
  EXPECT_EQ(source_->FramesRead(), 3000);
  renderer_->SetMuted(false);
  renderer_->SetVolume(0.25F);
  const std::vector<float> back = Render(1000);
  EXPECT_FLOAT_EQ(back[999], 0.25F);
}

TEST_F(AudioRendererTest, FlushResetsPositionAndFadesIn) {
  Build();
  Render(2000);
  EXPECT_EQ(renderer_->Generation(), 0U);
  renderer_->Flush();
  EXPECT_EQ(renderer_->Generation(), 1U);
  EXPECT_EQ(renderer_->MediaFramesDelivered(), 0);
  EXPECT_EQ(renderer_->EstimatePlayedFrames(1'000'000'000, 0), 0);
  const std::vector<float> output = Render(1000);
  EXPECT_LT(output[0], 0.01F);
  EXPECT_FLOAT_EQ(output[kLastFrame * 2], 0.5F);
  EXPECT_EQ(renderer_->MediaFramesDelivered(), 1000);
}

TEST_F(AudioRendererTest, FlushWaitsForTheCallbackInFlight) {
  Build();
  std::atomic<bool> stop{false};
  std::thread audio([&] {
    std::vector<float> output(kFlushFrames * 2);
    while (!stop.load()) {
      renderer_->Render(output);
    }
  });
  for (int index = 0; index < 50; ++index) {
    renderer_->Flush();
  }
  stop.store(true);
  audio.join();
  EXPECT_EQ(renderer_->Generation(), 50U);
}

class DeviceModelTest : public AudioRendererTest {
 protected:
  static constexpr int64_t kPeriodFrames = 480;
  static constexpr int64_t kPeriodNanoseconds = 10'000'000;

  void RunCallback(int64_t index) {
    clock_.Set(index * kPeriodNanoseconds);
    Render(static_cast<size_t>(kPeriodFrames));
  }

  [[nodiscard]] static int64_t HeardMediaFrames(int64_t now_nanoseconds) {
    const int64_t heard_device =
        ((kPeriodFrames * now_nanoseconds) / kPeriodNanoseconds) -
        kPeriodFrames;
    return std::max<int64_t>(0, heard_device);
  }
};

TEST_F(DeviceModelTest, PositionStaysWithinOneDeviceBufferPeriod) {
  Build();
  source_->SetMode(ScriptedSource::Mode::kConstant);
  int64_t previous = 0;
  for (int64_t index = 0; index < 200; ++index) {
    RunCallback(index);
    for (int64_t step = 0; step < 10; ++step) {
      const int64_t now =
          (index * kPeriodNanoseconds) + (step * kPeriodNanoseconds / 10);
      const int64_t estimate =
          renderer_->EstimatePlayedFrames(now, kPeriodFrames);
      const int64_t truth = HeardMediaFrames(now);
      EXPECT_LE(std::llabs(estimate - truth), kPeriodFrames) << "at " << now;
      EXPECT_GE(estimate, previous);
      previous = estimate;
    }
  }
}

TEST_F(DeviceModelTest, PositionHoldsAcrossAnUnderrunAndResumesWithoutAJump) {
  Build();
  for (int64_t index = 0; index < 20; ++index) {
    RunCallback(index);
  }
  const int64_t delivered = renderer_->MediaFramesDelivered();
  source_->SetFrameLimit(delivered);
  for (int64_t index = 20; index < 60; ++index) {
    RunCallback(index);
  }
  EXPECT_EQ(renderer_->MediaFramesDelivered(), delivered);
  const int64_t held =
      renderer_->EstimatePlayedFrames(60 * kPeriodNanoseconds, kPeriodFrames);
  EXPECT_EQ(held, delivered);
  source_->SetFrameLimit(-1);
  RunCallback(60);
  const int64_t after = renderer_->EstimatePlayedFrames(
      (60 * kPeriodNanoseconds) + (kPeriodNanoseconds / 2), kPeriodFrames);
  EXPECT_GE(after, held);
  EXPECT_LE(after - held, kPeriodFrames * 2);
}

TEST_F(DeviceModelTest, PositionNeverExceedsDeliveredFrames) {
  Build();
  RunCallback(0);
  RunCallback(1);
  const int64_t delivered = renderer_->MediaFramesDelivered();
  EXPECT_LE(renderer_->EstimatePlayedFrames(5'000'000'000, 0), delivered);
  EXPECT_EQ(renderer_->EstimatePlayedFrames(kPeriodNanoseconds, 1'000'000), 0);
}

}
}
