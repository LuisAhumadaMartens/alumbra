#include "penumbra/interface/stream_info.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"

namespace penumbra {
namespace {

TEST(StreamInfoTest, DerivesTheStreamTypeFromItsFormat) {
  VideoInfo video_format;
  video_format.coded_width = 1920;
  video_format.coded_height = 1080;
  StreamInfo video;
  video.format = video_format;
  EXPECT_EQ(video.type(), StreamType::kVideo);
  AudioInfo audio_format;
  audio_format.sample_rate = 48'000;
  audio_format.channel_count = 2;
  StreamInfo audio;
  audio.format = audio_format;
  EXPECT_EQ(audio.type(), StreamType::kAudio);
  StreamInfo text;
  text.format = TextInfo{};
  EXPECT_EQ(text.type(), StreamType::kText);
}

TEST(StreamInfoTest, DefaultsToUnspecifiedVideoProperties) {
  const VideoInfo video;
  EXPECT_EQ(video.sample_aspect_ratio, Rational::Of(1, 1));
  EXPECT_FALSE(video.frame_rate.is_positive());
  EXPECT_EQ(video.color.primaries, ColorSpace::kUnspecified);
  EXPECT_EQ(video.color.transfer, ColorSpace::kUnspecified);
  EXPECT_EQ(video.color.matrix, ColorSpace::kUnspecified);
  EXPECT_FALSE(video.color.full_range);
  EXPECT_EQ(video.color.chroma_siting, ChromaSiting::kUnspecified);
}

TEST(StreamInfoTest, ColorSpaceEqualityIncludesChromaSiting) {
  ColorSpace left;
  left.chroma_siting = ChromaSiting::kLeft;
  ColorSpace center;
  center.chroma_siting = ChromaSiting::kCenter;
  EXPECT_NE(left, center);
  center.chroma_siting = ChromaSiting::kLeft;
  EXPECT_EQ(left, center);
}

TEST(StreamInfoTest, CarriesCodecConfigurationWithoutCopying) {
  const SharedBytes avcc = SharedBytes::Adopt({0x01, 0x64, 0x00, 0x1F});
  StreamInfo info;
  info.codec = CodecId::kH264;
  info.codec_string = "avc1.64001f";
  info.codec_config = avcc;
  info.time_base = Rational::Of(1, 90'000);
  info.language = "es-419";
  VideoInfo format;
  format.frame_rate = Rational::Of(30'000, 1001);
  info.format = format;
  const StreamInfo copy = info;
  EXPECT_EQ(copy.codec_config.data(), avcc.data());
  EXPECT_EQ(std::get<VideoInfo>(copy.format).frame_rate,
            Rational::Of(30'000, 1001));
}

TEST(StreamInfoTest, Stringifies) {
  EXPECT_EQ(absl::StrCat(CodecId::kAv1), "av1");
  EXPECT_EQ(absl::StrCat(StreamType::kAudio), "audio");
}

TEST(DecodedFrameTest, PortOwnedStorageOutlivesTheDescriptorCopies) {
  bool released = false;
  auto pixels = std::make_shared<std::vector<uint8_t>>(64 * 48 * 3 / 2, 0x80);
  DecodedVideoFrame frame;
  frame.presentation_time = MediaTime::FromMilliseconds(40);
  frame.duration = TimeDelta::FromMilliseconds(40);
  frame.width = 64;
  frame.height = 48;
  frame.format = PixelFormat::kI420;
  frame.planes[0] = {.data = pixels->data(), .stride = 64};
  frame.planes[1] = {
      .data = std::span(*pixels).subspan(std::size_t{64} * 48).data(),
      .stride = 32};
  frame.planes[2] = {
      .data = std::span(*pixels)
                  .subspan((std::size_t{64} * 48) + (std::size_t{32} * 24))
                  .data(),
      .stride = 32};
  frame.storage = std::shared_ptr<const void>(
      pixels.get(), [&released, storage = pixels](const void*) {
        released = true;
        static_cast<void>(storage);
      });
  pixels.reset();
  DecodedVideoFrame queued = frame;
  frame = DecodedVideoFrame();
  EXPECT_FALSE(released);
  EXPECT_EQ(*queued.planes[0].data, 0x80);
  queued = DecodedVideoFrame();
  EXPECT_TRUE(released);
}

TEST(DecodedFrameTest, MovingAVideoFrameLeavesTheSourceEmpty) {
  auto pixels = std::make_shared<std::vector<uint8_t>>(16, 0x10);
  DecodedVideoFrame source;
  source.width = 4;
  source.height = 2;
  source.format = PixelFormat::kI420;
  source.planes[0] = {.data = pixels->data(), .stride = 4};
  source.storage = pixels;
  const DecodedVideoFrame& moved_from = source;

  DecodedVideoFrame constructed(std::move(source));
  EXPECT_EQ(constructed.planes[0].data, pixels->data());
  EXPECT_EQ(constructed.width, 4);
  EXPECT_EQ(constructed.storage, pixels);
  EXPECT_EQ(moved_from.planes[0].data, nullptr);
  EXPECT_EQ(moved_from.width, 0);
  EXPECT_EQ(moved_from.format, PixelFormat::kUnknown);
  EXPECT_EQ(moved_from.storage, nullptr);
  EXPECT_EQ(pixels.use_count(), 2);

  DecodedVideoFrame assigned;
  const DecodedVideoFrame& assigned_from = constructed;
  assigned = std::move(constructed);
  EXPECT_EQ(assigned.planes[0].data, pixels->data());
  EXPECT_EQ(assigned.height, 2);
  EXPECT_EQ(assigned_from.planes[0].data, nullptr);
  EXPECT_EQ(assigned_from.height, 0);
  EXPECT_EQ(assigned_from.storage, nullptr);
  EXPECT_EQ(pixels.use_count(), 2);
}

TEST(DecodedFrameTest, MovingAnAudioBufferLeavesTheSourceEmpty) {
  auto samples = std::make_shared<std::vector<float>>(8, 0.0F);
  DecodedAudioBuffer source;
  source.frame_count = 4;
  source.channel_count = 2;
  source.planes[0] = reinterpret_cast<const uint8_t*>(samples->data());
  source.storage = samples;
  const DecodedAudioBuffer& moved_from = source;

  DecodedAudioBuffer constructed(std::move(source));
  EXPECT_EQ(constructed.frame_count, 4);
  EXPECT_NE(constructed.planes[0], nullptr);
  EXPECT_EQ(moved_from.frame_count, 0);
  EXPECT_EQ(moved_from.planes[0], nullptr);
  EXPECT_EQ(moved_from.storage, nullptr);

  DecodedAudioBuffer assigned;
  const DecodedAudioBuffer& assigned_from = constructed;
  assigned = std::move(constructed);
  EXPECT_EQ(assigned.channel_count, 2);
  EXPECT_EQ(assigned_from.channel_count, 0);
  EXPECT_EQ(assigned_from.planes[0], nullptr);
  EXPECT_EQ(samples.use_count(), 2);
}

TEST(DecodedFrameTest, AudioBuffersDescribePortMemory) {
  auto samples = std::make_shared<std::vector<float>>(960 * 2, 0.0F);
  DecodedAudioBuffer buffer;
  buffer.presentation_time = MediaTime::Zero();
  buffer.frame_count = 960;
  buffer.sample_rate = 48'000;
  buffer.channel_count = 2;
  buffer.format = SampleFormat::kF32;
  buffer.planes[0] = reinterpret_cast<const uint8_t*>(samples->data());
  buffer.storage = samples;
  EXPECT_EQ(buffer.planes[1], nullptr);
  EXPECT_EQ(samples.use_count(), 2);
}

}
}
