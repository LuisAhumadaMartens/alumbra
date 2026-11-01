#ifndef PENUMBRA_INTERFACE_DECODED_FRAME_H_
#define PENUMBRA_INTERFACE_DECODED_FRAME_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {

enum class PixelFormat {
  kUnknown,
  kI420,
  kNv12,
  kP010,
  kOpaque,
};

struct VideoPlane {
  const uint8_t* data = nullptr;
  int32_t stride = 0;
};

struct DecodedVideoFrame {
  static constexpr size_t kMaxPlanes = 3;

  MediaTime presentation_time = MediaTime::Unknown();
  TimeDelta duration = TimeDelta::Unknown();
  int32_t width = 0;
  int32_t height = 0;
  PixelFormat format = PixelFormat::kUnknown;
  std::array<VideoPlane, kMaxPlanes> planes{};
  ColorSpace color;
  std::shared_ptr<const void> storage;
};

struct DecodedAudioBuffer {
  static constexpr size_t kMaxPlanes = 8;

  MediaTime presentation_time = MediaTime::Unknown();
  int32_t frame_count = 0;
  int32_t sample_rate = 0;
  int32_t channel_count = 0;
  SampleFormat format = SampleFormat::kUnknown;
  std::array<const uint8_t*, kMaxPlanes> planes{};
  std::shared_ptr<const void> storage;
};

}

#endif
