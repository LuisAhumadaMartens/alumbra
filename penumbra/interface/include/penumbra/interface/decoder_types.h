#ifndef PENUMBRA_INTERFACE_DECODER_TYPES_H_
#define PENUMBRA_INTERFACE_DECODER_TYPES_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/stream_info.h"

namespace penumbra {

enum class DecoderKind : uint8_t {
  kHardware,
  kPlatformSoftware,
  kBundledSoftware,
};

enum class DecoderOutput : uint8_t {
  kCpuBuffer,
  kGpuSurface,
};

enum class HardwarePreference : uint8_t {
  kAuto,
  kAlways,
  kNever,
};

struct DecoderCandidate {
  std::string name;
  DecoderKind kind = DecoderKind::kBundledSoftware;
  DecoderOutput output = DecoderOutput::kCpuBuffer;
  bool secure = false;
  std::vector<CodecId> codecs;
  int32_t max_width = 0;
  int32_t max_height = 0;
  std::vector<std::string> quirks;
};

struct DecoderSettings {
  HardwarePreference prefer_hardware = HardwarePreference::kAuto;
  bool fallback_enabled = true;
  int32_t dropped_frames_max_percent = 10;
  int32_t dropped_frames_window_ms = 5000;
  std::vector<std::string> preferred_decoders;
  std::vector<std::string> blocklist_extra_decoders;
  std::vector<std::string> blocklist_ignored_entries;
};

constexpr std::string_view DecoderKindName(DecoderKind kind) {
  switch (kind) {
    case DecoderKind::kHardware:
      return "hardware";
    case DecoderKind::kPlatformSoftware:
      return "platform_software";
    case DecoderKind::kBundledSoftware:
      return "bundled_software";
  }
  return "unknown";
}

constexpr std::string_view DecoderOutputName(DecoderOutput output) {
  switch (output) {
    case DecoderOutput::kCpuBuffer:
      return "cpu_buffer";
    case DecoderOutput::kGpuSurface:
      return "gpu_surface";
  }
  return "unknown";
}

}

#endif
