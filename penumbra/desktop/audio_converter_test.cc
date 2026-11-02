#include "penumbra/desktop/audio_converter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

DecodedAudioBuffer InterleavedS16(const std::vector<int16_t>& samples,
                                  int32_t channels, int32_t sample_rate,
                                  MediaTime time) {
  DecodedAudioBuffer buffer;
  buffer.presentation_time = time;
  buffer.frame_count = static_cast<int32_t>(samples.size()) / channels;
  buffer.sample_rate = sample_rate;
  buffer.channel_count = channels;
  buffer.format = SampleFormat::kS16;
  buffer.planes[0] = reinterpret_cast<const uint8_t*>(samples.data());
  return buffer;
}

AudioConversionConfig Config(int32_t output_rate, int32_t output_channels,
                             uint64_t output_mask = 0,
                             AudioDownmixMode downmix = AudioDownmixMode::kAuto,
                             float volume = 1.0F, int32_t ramp_frames = 128) {
  AudioConversionConfig config;
  config.output_sample_rate = output_rate;
  config.output_channel_count = output_channels;
  config.output_channel_mask = output_mask;
  config.downmix_mode = downmix;
  config.volume = volume;
  config.gain_ramp_frames = ramp_frames;
  return config;
}

std::vector<int16_t> StereoSineSamples(size_t frames) {
  std::vector<int16_t> samples(frames * 2U);
  for (size_t frame = 0; frame < frames; ++frame) {
    const auto sample = static_cast<int16_t>(frame % 2U == 0U ? 12000 : -12000);
    samples[frame * 2U] = sample;
    samples[(frame * 2U) + 1U] = sample;
  }
  return samples;
}

void ExpectFivePointOneChannelRoute(size_t channel, float expected_left,
                                    float expected_right) {
  std::vector<int16_t> samples(size_t{128} * 6U);
  for (size_t frame = 0; frame < size_t{128}; ++frame) {
    samples.at((frame * size_t{6}) + channel) = 10000;
  }
  auto converter = AudioConverter::Create(Config(48000, 2));
  ASSERT_TRUE(converter.ok()) << converter.status();
  const auto input = InterleavedS16(samples, 6, 48000, MediaTime::Zero());
  auto converted = (*converter)->Convert(input, 0x3f);
  ASSERT_TRUE(converted.ok()) << converted.status();
  const size_t sample_index = size_t{64} * 2U;
  EXPECT_NEAR(converted->samples.at(sample_index), expected_left, 0.0001F);
  EXPECT_NEAR(converted->samples.at(sample_index + 1U), expected_right,
              0.0001F);
}

TEST(AudioConverterTest, PreservesFivePointOneChannelOrder) {
  const std::array<int16_t, 6> samples = {1000, 2000, 3000, 4000, 5000, 6000};
  auto converter =
      AudioConverter::Create(Config(48000, 6, 0x3f, AudioDownmixMode::kNone));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted =
      (*converter)
          ->Convert(InterleavedS16({samples.begin(), samples.end()}, 6, 48000,
                                   MediaTime::FromSeconds(2)),
                    0x3f);
  ASSERT_TRUE(converted.ok()) << converted.status();
  ASSERT_EQ(converted->frame_count, 1);
  ASSERT_EQ(converted->samples.size(), 6U);
  EXPECT_NEAR(converted->samples[0], 1000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[1], 2000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[2], 3000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[3], 4000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[4], 5000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[5], 6000.0F / 32768.0F, 0.0001F);
  EXPECT_EQ(converted->presentation_time, MediaTime::FromSeconds(2));
}

TEST(AudioConverterTest, PreservesSevenPointOneChannelOrder) {
  const std::array<int16_t, 8> samples = {1000, 2000, 3000, 4000,
                                          5000, 6000, 7000, 8000};
  auto converter =
      AudioConverter::Create(Config(48000, 8, 0x63f, AudioDownmixMode::kNone));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted =
      (*converter)
          ->Convert(InterleavedS16({samples.begin(), samples.end()}, 8, 48000,
                                   MediaTime::Zero()),
                    0x63f);
  ASSERT_TRUE(converted.ok()) << converted.status();
  ASSERT_EQ(converted->samples.size(), 8U);
  EXPECT_NEAR(converted->samples[0], 1000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[1], 2000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[2], 3000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[3], 4000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[4], 5000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[5], 6000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[6], 7000.0F / 32768.0F, 0.0001F);
  EXPECT_NEAR(converted->samples[7], 8000.0F / 32768.0F, 0.0001F);
}

TEST(AudioConverterTest, AppliesBs775StereoDownmixWithoutLfe) {
  const std::array<int16_t, 6> samples = {0, 0, 10000, 12000, 0, 0};
  auto converter = AudioConverter::Create(Config(48000, 2));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted =
      (*converter)
          ->Convert(InterleavedS16({samples.begin(), samples.end()}, 6, 48000,
                                   MediaTime::Zero()),
                    0x3f);
  ASSERT_TRUE(converted.ok()) << converted.status();
  ASSERT_EQ(converted->samples.size(), 2U);
  EXPECT_NEAR(converted->samples[0], (10000.0F / 32768.0F) * 0.70710678F,
              0.0001F);
  EXPECT_NEAR(converted->samples[1], (10000.0F / 32768.0F) * 0.70710678F,
              0.0001F);
}

TEST(AudioConverterTest, RoutesEachFivePointOneChannelToStereo) {
  ExpectFivePointOneChannelRoute(0, 10000.0F / 32768.0F, 0.0F);
  ExpectFivePointOneChannelRoute(1, 0.0F, 10000.0F / 32768.0F);
  ExpectFivePointOneChannelRoute(2, 10000.0F * 0.70710678F / 32768.0F,
                                 10000.0F * 0.70710678F / 32768.0F);
  ExpectFivePointOneChannelRoute(3, 0.0F, 0.0F);
  ExpectFivePointOneChannelRoute(4, 10000.0F * 0.70710678F / 32768.0F, 0.0F);
  ExpectFivePointOneChannelRoute(5, 0.0F, 10000.0F * 0.70710678F / 32768.0F);
}

TEST(AudioConverterTest, UsesBitstreamDownmixCoefficientsWhenPresent) {
  std::vector<int16_t> samples(size_t{128} * 6U);
  for (size_t frame = 0; frame < size_t{128}; ++frame) {
    samples[(frame * 6U) + 3U] = 10000;
  }
  DecodedAudioBuffer input =
      InterleavedS16(samples, 6, 48000, MediaTime::Zero());
  input.has_downmix_metadata = true;
  input.lfe_mix_level = 0.25F;
  auto converter = AudioConverter::Create(Config(48000, 2));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted = (*converter)->Convert(input, 0x3f);
  ASSERT_TRUE(converted.ok()) << converted.status();
  const size_t sample_index = size_t{64} * 2U;
  EXPECT_NEAR(converted->samples[sample_index], 10000.0F * 0.25F / 32768.0F,
              0.0001F);
  EXPECT_NEAR(converted->samples[sample_index + 1U],
              10000.0F * 0.25F / 32768.0F, 0.0001F);
}

TEST(AudioConverterTest, DownmixesOpusFamilyOneChannelOrder) {
  std::vector<int16_t> samples(size_t{128} * 6U);
  const std::vector<int16_t> channel_values = {1000, 3000, 2000,
                                               5000, 6000, 4000};
  for (size_t frame = 0; frame < size_t{128}; ++frame) {
    for (size_t channel = 0; channel < channel_values.size(); ++channel) {
      samples[(frame * channel_values.size()) + channel] =
          channel_values.at(channel);
    }
  }
  DecodedAudioBuffer input =
      InterleavedS16(samples, 6, 48000, MediaTime::Zero());
  input.channel_layout = {
      AudioChannel::kFrontLeft,  AudioChannel::kFrontCenter,
      AudioChannel::kFrontRight, AudioChannel::kSideLeft,
      AudioChannel::kSideRight,  AudioChannel::kLowFrequency,
      AudioChannel::kUnknown,    AudioChannel::kUnknown};
  auto converter = AudioConverter::Create(Config(48000, 2));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted = (*converter)->Convert(input, 0);
  ASSERT_TRUE(converted.ok()) << converted.status();
  const size_t sample_index = size_t{64} * 2U;
  EXPECT_NEAR(
      converted->samples[sample_index],
      (1000.0F + 3000.0F * 0.70710678F + 5000.0F * 0.70710678F) / 32768.0F,
      0.0001F);
  EXPECT_NEAR(
      converted->samples[sample_index + 1U],
      (2000.0F + 3000.0F * 0.70710678F + 6000.0F * 0.70710678F) / 32768.0F,
      0.0001F);
}

TEST(AudioConverterTest, ConvertsPlanarFloatAndAppliesVolume) {
  const std::array<float, 4> left = {0.2F, -0.2F, 0.4F, -0.4F};
  const std::array<float, 4> right = {-0.1F, 0.1F, -0.3F, 0.3F};
  DecodedAudioBuffer input;
  input.presentation_time = MediaTime::FromMilliseconds(50);
  input.frame_count = 4;
  input.sample_rate = 48000;
  input.channel_count = 2;
  input.format = SampleFormat::kF32Planar;
  input.planes[0] = reinterpret_cast<const uint8_t*>(left.data());
  input.planes[1] = reinterpret_cast<const uint8_t*>(right.data());
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 0.5F, 0));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(converted.ok()) << converted.status();
  EXPECT_NEAR(converted->samples[0], 0.1F, 0.0001F);
  EXPECT_NEAR(converted->samples[1], -0.05F, 0.0001F);
  EXPECT_EQ(converted->presentation_time, input.presentation_time);
}

TEST(AudioConverterTest, ResamplesToDeviceRateAndKeepsContiguousTimestamps) {
  const std::vector<int16_t> samples = StereoSineSamples(size_t{4410});
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 1.0F, 0));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto input = InterleavedS16(samples, 2, 44100, MediaTime::FromSeconds(3));
  auto first = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_GT(first->frame_count, 0);
  EXPECT_EQ(first->sample_rate, 48000);
  input.presentation_time =
      MediaTime::FromSeconds(3) + TimeDelta::FromMicroseconds(100000);
  auto second = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(
      second->presentation_time,
      first->presentation_time +
          TimeDelta::FromMicroseconds(
              (static_cast<int64_t>(first->frame_count) * 1'000'000 + 24000) /
              48000));
  EXPECT_EQ(second->sample_rate, 48000);
}

TEST(AudioConverterTest, DrainReturnsResamplerDelaySamples) {
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 1.0F, 0));
  ASSERT_TRUE(converter.ok()) << converter.status();
  int32_t output_frames = 0;
  const std::vector<int16_t> samples(size_t{441} * 2U, 12000);
  for (int32_t chunk = 0; chunk < 100; ++chunk) {
    auto converted =
        (*converter)
            ->Convert(InterleavedS16(samples, 2, 44100,
                                     MediaTime::FromMicroseconds(
                                         static_cast<int64_t>(chunk) * 10000)),
                      0x3);
    ASSERT_TRUE(converted.ok()) << converted.status();
    output_frames += converted->frame_count;
  }
  auto drained = (*converter)->Drain();
  ASSERT_TRUE(drained.ok()) << drained.status();
  output_frames += drained->frame_count;
  EXPECT_EQ(output_frames, 48000);
  auto empty = (*converter)->Drain();
  ASSERT_TRUE(empty.ok()) << empty.status();
  EXPECT_EQ(empty->frame_count, 0);
}

TEST(AudioConverterTest, ConvertsPlanarSignedThirtyTwoBitSamples) {
  const std::array<int32_t, 2> left = {1073741824, -1073741824};
  const std::array<int32_t, 2> right = {-536870912, 536870912};
  DecodedAudioBuffer input;
  input.presentation_time = MediaTime::Zero();
  input.frame_count = 2;
  input.sample_rate = 48000;
  input.channel_count = 2;
  input.format = SampleFormat::kS32Planar;
  input.planes[0] = reinterpret_cast<const uint8_t*>(left.data());
  input.planes[1] = reinterpret_cast<const uint8_t*>(right.data());
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 1.0F, 0));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(converted.ok()) << converted.status();
  ASSERT_EQ(converted->samples.size(), 4U);
  EXPECT_NEAR(converted->samples[0], 0.5F, 0.0001F);
  EXPECT_NEAR(converted->samples[1], -0.25F, 0.0001F);
}

TEST(AudioConverterTest, ConvertsInterleavedFloatSamples) {
  const std::array<float, 4> samples = {0.25F, -0.25F, 0.5F, -0.5F};
  DecodedAudioBuffer input;
  input.presentation_time = MediaTime::Zero();
  input.frame_count = 2;
  input.sample_rate = 48000;
  input.channel_count = 2;
  input.format = SampleFormat::kF32;
  input.planes[0] = reinterpret_cast<const uint8_t*>(samples.data());
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 1.0F, 0));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto converted = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(converted.ok()) << converted.status();
  EXPECT_NEAR(converted->samples[0], 0.25F, 0.0001F);
  EXPECT_NEAR(converted->samples[1], -0.25F, 0.0001F);
}

TEST(AudioConverterTest, MuteAndVolumeChangesUseGainRamp) {
  const std::vector<int16_t> samples(32, 16000);
  auto converter = AudioConverter::Create(
      Config(48000, 2, 0, AudioDownmixMode::kAuto, 1.0F, 8));
  ASSERT_TRUE(converter.ok()) << converter.status();
  auto input = InterleavedS16(samples, 2, 48000, MediaTime::Zero());
  auto first = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_NEAR(first->samples.front(), 16000.0F / 32768.0F, 0.0001F);
  (*converter)->SetMuted(true);
  input.presentation_time = MediaTime::FromMilliseconds(1);
  auto muted = (*converter)->Convert(input, 0x3);
  ASSERT_TRUE(muted.ok()) << muted.status();
  EXPECT_GT(muted->samples.front(), 0.0F);
  EXPECT_NEAR(muted->samples[14], 0.0F, 0.0001F);
  EXPECT_NEAR(muted->samples.back(), 0.0F, 0.0001F);
}

TEST(AudioConverterTest, RejectsDisabledDownmixAndInvalidTimeStretch) {
  auto converter =
      AudioConverter::Create(Config(48000, 2, 0, AudioDownmixMode::kNone));
  ASSERT_TRUE(converter.ok()) << converter.status();
  const std::array<int16_t, 6> samples{};
  auto converted =
      (*converter)
          ->Convert(InterleavedS16({samples.begin(), samples.end()}, 6, 48000,
                                   MediaTime::Zero()),
                    0x3f);
  EXPECT_FALSE(converted.ok());
  EXPECT_FALSE((*converter)->SetTimeStretchRatio(0.0).ok());
}

}
}
