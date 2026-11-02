#include "umbra/pipeline/buffer_manager.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::pipeline {
namespace {

penumbra::StreamConfig Stream(std::uint32_t track_id,
                              penumbra::StreamType type) {
  penumbra::StreamConfig stream;
  stream.info.track_id = track_id;
  stream.info.time_base = penumbra::Rational::Of(1, 1000);
  if (type == penumbra::StreamType::kAudio) {
    stream.info.format = penumbra::AudioInfo{};
  }
  return stream;
}

penumbra::EncodedSample Sample(std::uint32_t track_id, std::int64_t start_ms,
                               std::int64_t duration_ms, std::size_t bytes,
                               bool key_frame = false) {
  penumbra::EncodedSample sample;
  sample.track_id = track_id;
  sample.data =
      penumbra::SharedBytes::Adopt(std::vector<std::uint8_t>(bytes, 0));
  sample.pts = {.value = start_ms,
                .time_base = penumbra::Rational::Of(1, 1000)};
  sample.duration = {.value = duration_ms,
                     .time_base = penumbra::Rational::Of(1, 1000)};
  sample.key_frame = key_frame;
  return sample;
}

TEST(BufferManagerTest, AccountsBytesByMediaType) {
  BufferManager manager;
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo),
                      Stream(2, penumbra::StreamType::kAudio)});

  manager.RecordSamples({Sample(1, 0, 1000, 1200), Sample(2, 0, 1000, 300)});

  const BufferManager::Accounting accounting = manager.GetAccounting();
  EXPECT_EQ(accounting.video_bytes, 1200U);
  EXPECT_EQ(accounting.audio_bytes, 300U);
}

TEST(BufferManagerTest, StopsFetchesAtForwardGoalAndRespectsByteBudget) {
  BufferManager::Config config;
  config.forward_goal_ms = 5000;
  config.video_max_bytes = 1000;
  config.audio_max_bytes = 1000;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});

  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::Zero(), 1000));
  manager.RecordSamples({Sample(1, 0, 1000, 1000)});
  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(4000),
                               1000));
  manager.RecordSamples({Sample(1, 1000, 1000, 1000)});
  EXPECT_FALSE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                                penumbra::MediaTime::FromMilliseconds(5000),
                                1000));
}

TEST(BufferManagerTest, KeepsRebufferThresholdWhenBudgetIsExhausted) {
  BufferManager::Config config;
  config.video_max_bytes = 100;
  config.rebuffer_threshold_ms = 4000;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  manager.RecordSamples({Sample(1, 0, 1000, 100)});

  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(1000),
                               1000));
  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(4000),
                               1000));
  manager.RecordSamples({Sample(1, 1000, 1000, 100)});
  EXPECT_FALSE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                                penumbra::MediaTime::FromMilliseconds(5000),
                                1000));
}

TEST(BufferManagerTest, PressureReducesBudgetAndForwardGoal) {
  BufferManager::Config config;
  config.forward_goal_ms = 20000;
  config.video_max_bytes = 1000;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  manager.RecordSamples({Sample(1, 0, 1000, 600)});
  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(1000),
                               1000));

  manager.SetMemoryPressure(penumbra::MemoryPressure::kCritical);

  EXPECT_EQ(manager.EffectiveForwardGoalMs(), 10000);
  EXPECT_FALSE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                                penumbra::MediaTime::FromMilliseconds(4000),
                                1000));
  manager.SetMemoryPressure(penumbra::MemoryPressure::kNormal);
  EXPECT_EQ(manager.EffectiveForwardGoalMs(), 20000);
}

TEST(BufferManagerTest, PortBudgetCanLowerTheConfiguredLimit) {
  BufferManager::Config config;
  config.video_max_bytes = 1000;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  manager.SetPortBudgets(
      {.video_max_bytes = 100, .audio_max_bytes = std::nullopt});
  manager.RecordSamples({Sample(1, 0, 1000, 100)});

  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(4000),
                               1000));
  manager.RecordSamples({Sample(1, 1000, 1000, 100)});
  EXPECT_FALSE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                                penumbra::MediaTime::FromMilliseconds(5000),
                                1000));
}

TEST(BufferManagerTest, EvictionRetainsBackGoalAndProtectedForwardRange) {
  BufferManager::Config config;
  config.back_goal_ms = 1000;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  manager.RecordSamples(
      {Sample(1, 0, 1000, 10, true), Sample(1, 1000, 1000, 20, false),
       Sample(1, 2000, 1000, 30, true), Sample(1, 3000, 1000, 40, false),
       Sample(1, 4000, 1000, 50, true)});

  EXPECT_EQ(manager.EvictBehind(penumbra::MediaTime::FromMilliseconds(4000)),
            30U);
  const BufferManager::Accounting accounting = manager.GetAccounting();
  EXPECT_EQ(accounting.video_bytes, 120U);
}

TEST(BufferManagerTest, PressureEvictionKeepsRebufferRange) {
  BufferManager::Config config;
  config.back_goal_ms = 0;
  config.rebuffer_threshold_ms = 1000;
  config.video_max_bytes = 100;
  BufferManager manager(config);
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  manager.RecordSamples(
      {Sample(1, 0, 1000, 10, true), Sample(1, 1000, 1000, 20, false),
       Sample(1, 2000, 1000, 30, true), Sample(1, 3000, 1000, 40, false),
       Sample(1, 4000, 1000, 50, true)});
  manager.SetMemoryPressure(penumbra::MemoryPressure::kCritical);

  EXPECT_EQ(manager.EvictBehind(penumbra::MediaTime::FromMilliseconds(1000)),
            120U);
  EXPECT_EQ(manager.GetAccounting().video_bytes, 30U);
}

TEST(BufferManagerTest, ConfigChangesAffectTheNextDecision) {
  BufferManager manager;
  manager.SetStreams({Stream(1, penumbra::StreamType::kVideo)});
  EXPECT_TRUE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                               penumbra::MediaTime::FromMilliseconds(20000),
                               1000));

  BufferManager::Config config;
  config.forward_goal_ms = 10000;
  ASSERT_TRUE(manager.SetConfig(config));

  EXPECT_FALSE(manager.CanFetch(1, penumbra::MediaTime::Zero(),
                                penumbra::MediaTime::FromMilliseconds(10000),
                                1000));
}

}
}
