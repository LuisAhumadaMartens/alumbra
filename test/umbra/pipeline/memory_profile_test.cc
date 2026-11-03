#include "umbra/pipeline/memory_profile.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "penumbra/interface/port_types.h"

namespace umbra::pipeline {
namespace {

TEST(MemoryProfileTest, AutoUsesAvailablePlatformSignals) {
  MemoryProfileSignals low_ram;
  low_ram.low_ram_device = true;
  const MemoryProfilePolicy low_ram_profile(MemoryProfileMode::kAuto, low_ram,
                                            1080);
  EXPECT_TRUE(low_ram_profile.IsLowMemory());

  MemoryProfileSignals total_memory;
  total_memory.total_memory_bytes = std::uint64_t{1024} * 1024U * 1024U;
  const MemoryProfilePolicy total_memory_profile(MemoryProfileMode::kAuto,
                                                 total_memory, 1080);
  EXPECT_TRUE(total_memory_profile.IsLowMemory());

  MemoryProfileSignals browser_memory;
  browser_memory.device_memory_gib = 1.0;
  const MemoryProfilePolicy browser_profile(MemoryProfileMode::kAuto,
                                            browser_memory, 1080);
  EXPECT_TRUE(browser_profile.IsLowMemory());

  const MemoryProfilePolicy no_signal(MemoryProfileMode::kAuto, {}, 1080);
  EXPECT_FALSE(no_signal.IsLowMemory());
}

TEST(MemoryProfileTest, ExplicitModeOverridesPlatformSignals) {
  MemoryProfileSignals low_ram;
  low_ram.low_ram_device = true;
  const MemoryProfilePolicy standard(MemoryProfileMode::kStandard, low_ram,
                                     1080);
  const MemoryProfilePolicy low_memory(MemoryProfileMode::kLowMemory, {}, 1080);
  EXPECT_FALSE(standard.IsLowMemory());
  EXPECT_TRUE(low_memory.IsLowMemory());
}

TEST(MemoryProfileTest, LowMemoryLimitsBoundBuffersAndResolution) {
  const MemoryProfilePolicy policy(MemoryProfileMode::kLowMemory, {}, 2160);
  EXPECT_EQ(policy.BufferConfig().forward_goal_ms, 10000);
  EXPECT_EQ(policy.BufferConfig().back_goal_ms, 2000);
  EXPECT_EQ(policy.BufferConfig().video_max_bytes, 32U * 1024U * 1024U);
  EXPECT_EQ(policy.BufferConfig().audio_max_bytes, 4U * 1024U * 1024U);
  EXPECT_EQ(policy.MaxVideoHeight(), 1080);
}

TEST(MemoryProfileTest, PressureLowersResolutionAndNormalRestoresIt) {
  MemoryProfilePolicy policy(MemoryProfileMode::kStandard, {}, 2160);
  policy.SetMemoryPressure(penumbra::MemoryPressure::kModerate);
  EXPECT_EQ(policy.MaxVideoHeight(), 720);
  policy.SetMemoryPressure(penumbra::MemoryPressure::kCritical);
  EXPECT_EQ(policy.MaxVideoHeight(), 480);
  policy.SetMemoryPressure(penumbra::MemoryPressure::kNormal);
  EXPECT_EQ(policy.MaxVideoHeight(), 2160);
}

TEST(MemoryProfileTest, ValidatesConfiguredHeightRange) {
  EXPECT_FALSE(MemoryProfilePolicy::ValidateMaxVideoHeight(239));
  EXPECT_TRUE(MemoryProfilePolicy::ValidateMaxVideoHeight(240));
  EXPECT_TRUE(MemoryProfilePolicy::ValidateMaxVideoHeight(2160));
  EXPECT_FALSE(MemoryProfilePolicy::ValidateMaxVideoHeight(2161));
}

}
}
