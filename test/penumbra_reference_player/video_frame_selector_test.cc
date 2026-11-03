#include "penumbra/reference_player/video_frame_selector.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"

namespace penumbra {
namespace {

DecodedVideoFrame Frame(int64_t start_us, int64_t duration_us) {
  DecodedVideoFrame frame;
  frame.presentation_time = MediaTime::FromMicroseconds(start_us);
  frame.duration = TimeDelta::FromMicroseconds(duration_us);
  return frame;
}

TEST(VideoFrameSelectorTest, HoldsAnEarlyFrameUntilItsPresentationTime) {
  VideoFrameSelector selector;
  const std::array frames{Frame(0, 40'000), Frame(40'000, 40'000)};
  const VideoFrameSelection selection =
      selector.Select(frames, MediaTime::FromMicroseconds(39'000));
  ASSERT_TRUE(selection.frame_index.has_value());
  EXPECT_EQ(*selection.frame_index, 0U);
  EXPECT_EQ(selection.dropped_frames, 0U);
}

TEST(VideoFrameSelectorTest, SlowDecodeDropsLateFramesAndSelectsTheNewestDue) {
  VideoFrameSelector selector;
  const std::array frames{Frame(0, 40'000), Frame(40'000, 40'000),
                          Frame(80'000, 40'000)};
  const VideoFrameSelection selection =
      selector.Select(frames, MediaTime::FromMicroseconds(110'001));
  ASSERT_TRUE(selection.frame_index.has_value());
  EXPECT_EQ(*selection.frame_index, 2U);
  EXPECT_EQ(selection.dropped_frames, 2U);
  EXPECT_EQ(selector.DroppedFrames(), 2U);
}

TEST(VideoFrameSelectorTest, LatenessThresholdIsConfigurable) {
  VideoFrameSelector selector(50);
  const std::array frames{Frame(0, 40'000)};
  EXPECT_EQ(selector.Select(frames, MediaTime::FromMicroseconds(20'000))
                .dropped_frames,
            0U);
  EXPECT_EQ(selector.Select(frames, MediaTime::FromMicroseconds(20'001))
                .dropped_frames,
            1U);
  selector.SetLatenessPercent(100);
  EXPECT_EQ(selector.Select(frames, MediaTime::FromMicroseconds(40'000))
                .dropped_frames,
            0U);
}

TEST(VideoFrameSelectorTest, CountersResetForANewPlaybackGeneration) {
  VideoFrameSelector selector;
  const std::array frames{Frame(0, 40'000)};
  static_cast<void>(selector.Select(frames, MediaTime::FromMilliseconds(100)));
  EXPECT_EQ(selector.DroppedFrames(), 1U);
  selector.Reset();
  EXPECT_EQ(selector.DroppedFrames(), 0U);
}

}
}
