#include "umbra/pipeline/memory_profile.h"

#include <algorithm>
#include <cstdint>

#include "penumbra/interface/port_types.h"

namespace umbra::pipeline {
namespace {

constexpr std::uint64_t kOneGibibyte = std::uint64_t{1024} * 1024U * 1024U;

bool SignalsLowMemory(const MemoryProfileSignals& signals) {
  return signals.low_ram_device ||
         (signals.total_memory_bytes.has_value() &&
          *signals.total_memory_bytes <= kOneGibibyte) ||
         (signals.device_memory_gib.has_value() &&
          *signals.device_memory_gib <= 1.0);
}

}

MemoryProfilePolicy::MemoryProfilePolicy(MemoryProfileMode mode,
                                         MemoryProfileSignals signals,
                                         int configured_max_video_height)
    : low_memory_(
          mode == MemoryProfileMode::kLowMemory ||
          (mode == MemoryProfileMode::kAuto && SignalsLowMemory(signals))),
      configured_max_video_height_(configured_max_video_height) {
  if (low_memory_) {
    buffer_.forward_goal_ms = 10000;
    buffer_.back_goal_ms = 2000;
    buffer_.video_max_bytes = std::uint64_t{32} * 1024U * 1024U;
    buffer_.audio_max_bytes = std::uint64_t{4} * 1024U * 1024U;
  }
}

bool MemoryProfilePolicy::ValidateMaxVideoHeight(int height) {
  return height >= 240 && height <= 2160;
}

int MemoryProfilePolicy::MaxVideoHeight() const {
  int height = low_memory_ ? std::min(configured_max_video_height_, 1080)
                           : configured_max_video_height_;
  if (pressure_ == penumbra::MemoryPressure::kModerate) {
    height = std::min(height, 720);
  } else if (pressure_ == penumbra::MemoryPressure::kCritical) {
    height = std::min(height, 480);
  }
  return height;
}

void MemoryProfilePolicy::SetMemoryPressure(penumbra::MemoryPressure pressure) {
  pressure_ = pressure;
}

}
