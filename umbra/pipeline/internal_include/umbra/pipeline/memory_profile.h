#ifndef UMBRA_PIPELINE_MEMORY_PROFILE_H_
#define UMBRA_PIPELINE_MEMORY_PROFILE_H_

#include <cstdint>
#include <optional>

#include "penumbra/interface/port_types.h"
#include "umbra/pipeline/buffer_manager.h"

namespace umbra::pipeline {

enum class MemoryProfileMode { kAuto, kStandard, kLowMemory };

struct MemoryProfileSignals {
  bool low_ram_device = false;
  std::optional<std::uint64_t> total_memory_bytes;
  std::optional<double> device_memory_gib;
};

struct MemoryProfileLimits {
  BufferManager::Config buffer;
  int max_video_height = 2160;
};

class MemoryProfilePolicy {
 public:
  MemoryProfilePolicy(MemoryProfileMode mode, MemoryProfileSignals signals,
                      int configured_max_video_height);

  [[nodiscard]] static bool ValidateMaxVideoHeight(int height);
  [[nodiscard]] bool IsLowMemory() const { return low_memory_; }
  [[nodiscard]] int MaxVideoHeight() const;
  [[nodiscard]] const BufferManager::Config& BufferConfig() const {
    return buffer_;
  }
  void SetMemoryPressure(penumbra::MemoryPressure pressure);

 private:
  bool low_memory_ = false;
  int configured_max_video_height_ = 2160;
  penumbra::MemoryPressure pressure_ = penumbra::MemoryPressure::kNormal;
  BufferManager::Config buffer_;
};

}

#endif
