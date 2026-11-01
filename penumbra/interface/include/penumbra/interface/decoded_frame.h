#ifndef PENUMBRA_INTERFACE_DECODED_FRAME_H_
#define PENUMBRA_INTERFACE_DECODED_FRAME_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {

enum class PixelFormat : uint8_t {
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

  DecodedVideoFrame() = default;
  ~DecodedVideoFrame() = default;
  DecodedVideoFrame(const DecodedVideoFrame&) = default;
  DecodedVideoFrame& operator=(const DecodedVideoFrame&) = default;
  DecodedVideoFrame(DecodedVideoFrame&& other) noexcept
      : presentation_time(std::exchange(other.presentation_time, MediaTime::Unknown())),
        duration(std::exchange(other.duration, TimeDelta::Unknown())),
        width(std::exchange(other.width, 0)),
        height(std::exchange(other.height, 0)),
        format(std::exchange(other.format, PixelFormat::kUnknown)),
        planes(std::exchange(other.planes, {})),
        color(std::exchange(other.color, {})),
        storage(std::move(other.storage)) {}
  DecodedVideoFrame& operator=(DecodedVideoFrame&& other) noexcept {
    if (this != &other) {
      *this = std::as_const(other);
      const DecodedVideoFrame empty;
      other = empty;
    }
    return *this;
  }

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

  DecodedAudioBuffer() = default;
  ~DecodedAudioBuffer() = default;
  DecodedAudioBuffer(const DecodedAudioBuffer&) = default;
  DecodedAudioBuffer& operator=(const DecodedAudioBuffer&) = default;
  DecodedAudioBuffer(DecodedAudioBuffer&& other) noexcept
      : presentation_time(std::exchange(other.presentation_time, MediaTime::Unknown())),
        frame_count(std::exchange(other.frame_count, 0)),
        sample_rate(std::exchange(other.sample_rate, 0)),
        channel_count(std::exchange(other.channel_count, 0)),
        format(std::exchange(other.format, SampleFormat::kUnknown)),
        planes(std::exchange(other.planes, {})),
        storage(std::move(other.storage)) {}
  DecodedAudioBuffer& operator=(DecodedAudioBuffer&& other) noexcept {
    if (this != &other) {
      *this = std::as_const(other);
      const DecodedAudioBuffer empty;
      other = empty;
    }
    return *this;
  }

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
