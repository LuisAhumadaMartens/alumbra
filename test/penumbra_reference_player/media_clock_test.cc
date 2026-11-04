#include "penumbra/reference_player/media_clock.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"

namespace penumbra {
namespace {

TEST(MediaClockTest, AdvancesWithRateAndFreezesWhenPaused) {
  MediaClock clock;
  clock.Reset(MediaTime::FromSeconds(3), 0);
  clock.SetRate(1.5, 0);
  clock.SetPaused(false, 0);
  EXPECT_EQ(clock.Now(2'000'000), MediaTime::FromSeconds(6));
  clock.SetPaused(true, 2'000'000);
  EXPECT_EQ(clock.Now(8'000'000), MediaTime::FromSeconds(6));
  clock.SetPaused(false, 8'000'000);
  EXPECT_EQ(clock.Now(10'000'000), MediaTime::FromSeconds(9));
}

TEST(MediaClockTest, AdvancesAtHalfAndDoublePlaybackRates) {
  for (const double rate : {0.5, 2.0}) {
    MediaClock clock;
    clock.Reset(MediaTime::Zero(), 0);
    clock.SetRate(rate, 0);
    clock.SetPaused(false, 0);
    EXPECT_EQ(clock.Now(4'000'000), MediaTime::FromSeconds(4.0 * rate));
  }
}

TEST(MediaClockTest, AppliesLatencyAndAudioOffsetToTheAudiblePosition) {
  MediaClock clock;
  clock.Reset(MediaTime::FromMicroseconds(1'850'000), 2'000'000);
  clock.SetRate(1.0, 2'000'000);
  clock.SetPaused(false, 2'000'000);
  clock.UpdateAudio({.media_time = MediaTime::FromSeconds(2),
                     .output_latency = TimeDelta::FromMilliseconds(100),
                     .audio_offset = TimeDelta::FromMilliseconds(50),
                     .observed_at_us = 2'000'000},
                    2'000'000);
  EXPECT_EQ(clock.Now(2'000'000), MediaTime::FromMicroseconds(1'850'000));
  EXPECT_EQ(clock.Now(3'000'000), MediaTime::FromMicroseconds(2'850'000));
}

TEST(MediaClockTest, RateAndSeekChangesKeepTimeMonotonicUntilReset) {
  MediaClock clock;
  clock.Reset(MediaTime::Zero(), 0);
  clock.SetRate(1.0, 0);
  clock.SetPaused(false, 0);
  int64_t previous = 0;
  for (int64_t step = 1; step <= 2000; ++step) {
    const int64_t now = step * 5'000;
    if (step == 200) {
      clock.SetRate(2.0, now);
    }
    if (step == 500) {
      clock.SetPaused(true, now);
    }
    if (step == 700) {
      clock.SetPaused(false, now);
    }
    if (step == 1200) {
      clock.SetResampleRatio(1.005, now);
    }
    const int64_t current = clock.Now(now).InMicroseconds();
    EXPECT_GE(current, previous);
    previous = current;
  }
  clock.Reset(MediaTime::FromSeconds(1), 10'000'000);
  EXPECT_EQ(clock.Now(10'000'000), MediaTime::FromSeconds(1));
}

TEST(MediaClockTest, AudioPositionInterpolationDoesNotRewind) {
  MediaClock clock;
  clock.Reset(MediaTime::Zero(), 0);
  clock.SetRate(1.0, 0);
  clock.SetPaused(false, 0);
  int64_t previous = 0;
  for (int64_t step = 1; step <= 1000; ++step) {
    const int64_t now = step * 10'000;
    const int64_t reported = (step / 2) * 20'000;
    clock.UpdateAudio({.media_time = MediaTime::FromMicroseconds(reported),
                       .output_latency = TimeDelta::FromMilliseconds(30),
                       .audio_offset = TimeDelta::Zero(),
                       .observed_at_us = now},
                      now);
    const int64_t current = clock.Now(now).InMicroseconds();
    EXPECT_GE(current, previous);
    previous = current;
  }
}

TEST(MediaClockTest, TracksOneHundredPartsPerMillionAudioDrift) {
  MediaClock clock;
  clock.Reset(MediaTime::Zero(), 20'000);
  clock.SetRate(1.0, 20'000);
  clock.SetPaused(false, 20'000);
  for (int64_t now = 20'000; now <= 600'020'000; now += 10'000) {
    const auto media_us = static_cast<int64_t>(
        (static_cast<long double>(now - 20'000) * 1.0001L) + 20'000);
    clock.UpdateAudio({.media_time = MediaTime::FromMicroseconds(media_us),
                       .output_latency = TimeDelta::FromMilliseconds(20),
                       .audio_offset = TimeDelta::Zero(),
                       .observed_at_us = now},
                      now);
  }
  EXPECT_NEAR(clock.Now(600'020'000).InMicroseconds(), 600'060'000, 1'000);
}

TEST(MediaClockTest, RouteLatencyChangeSlewsWithoutRewinding) {
  MediaClock clock;
  clock.Reset(MediaTime::Zero(), 20'000);
  clock.SetRate(1.0, 20'000);
  clock.SetPaused(false, 20'000);
  clock.UpdateAudio({.media_time = MediaTime::FromMilliseconds(20),
                     .output_latency = TimeDelta::FromMilliseconds(20),
                     .audio_offset = TimeDelta::Zero(),
                     .observed_at_us = 20'000},
                    20'000);
  const int64_t before = clock.Now(120'000).InMicroseconds();
  clock.SlewAudioLatency(TimeDelta::FromMilliseconds(120), TimeDelta::Zero(),
                         120'000, TimeDelta::FromMilliseconds(300));
  int64_t previous = clock.Now(120'000).InMicroseconds();
  EXPECT_GE(previous, before);
  for (int64_t now = 130'000; now <= 420'000; now += 10'000) {
    const int64_t current = clock.Now(now).InMicroseconds();
    EXPECT_GE(current, previous);
    previous = current;
  }
}

}
}
