#include "penumbra/reference_player/audio_trim.h"

#include <array>
#include <cstdint>
#include <span>

#include "gtest/gtest.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::reference_player {
namespace {

TEST(AudioTrimTest, DropsBuffersBeforeTargetAndKeepsTargetPending) {
  std::array<uint8_t, 8> samples{};
  DecodedAudioBuffer source;
  source.presentation_time = MediaTime::Zero();
  source.frame_count = 4;
  source.sample_rate = 48000;
  source.channel_count = 1;
  source.format = SampleFormat::kS16;
  source.planes[0] = samples.data();
  bool reached_target = true;

  const auto result = TrimAudioBuffer(source, MediaTime::FromMilliseconds(100),
                                      &reached_target);

  EXPECT_FALSE(result.has_value());
  EXPECT_FALSE(reached_target);
}

TEST(AudioTrimTest, TrimsPackedAudioToFirstSampleAtOrAfterTarget) {
  std::array<uint8_t, 40> samples{};
  DecodedAudioBuffer source;
  source.presentation_time = MediaTime::Zero();
  source.frame_count = 10;
  source.sample_rate = 1000;
  source.channel_count = 2;
  source.format = SampleFormat::kS16;
  source.planes[0] = samples.data();
  bool reached_target = false;

  const auto result = TrimAudioBuffer(source, MediaTime::FromMicroseconds(3500),
                                      &reached_target);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(reached_target);
  EXPECT_EQ(result->frame_count, 6);
  EXPECT_EQ(result->planes[0], std::span<uint8_t>(samples).subspan(16).data());
}

TEST(AudioTrimTest, TrimsPlanarAudioWithoutMovingOtherChannels) {
  std::array<uint8_t, 20> left{};
  std::array<uint8_t, 20> right{};
  DecodedAudioBuffer source;
  source.presentation_time = MediaTime::Zero();
  source.frame_count = 10;
  source.sample_rate = 1000;
  source.channel_count = 2;
  source.format = SampleFormat::kS16Planar;
  source.planes[0] = left.data();
  source.planes[1] = right.data();
  bool reached_target = false;

  const auto result =
      TrimAudioBuffer(source, MediaTime::FromMilliseconds(4), &reached_target);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->frame_count, 6);
  EXPECT_EQ(result->planes[0], std::span<uint8_t>(left).subspan(8).data());
  EXPECT_EQ(result->planes[1], std::span<uint8_t>(right).subspan(8).data());
}

}
}
