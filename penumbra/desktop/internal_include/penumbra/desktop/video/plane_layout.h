#ifndef PENUMBRA_DESKTOP_VIDEO_PLANE_LAYOUT_H_
#define PENUMBRA_DESKTOP_VIDEO_PLANE_LAYOUT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "penumbra/interface/decoded_frame.h"

namespace penumbra::desktop {

enum class PlaneTexelFormat : uint8_t {
  kR8,
  kR8G8,
  kR16,
  kR16G16,
};

struct PlaneSpec {
  PlaneTexelFormat format = PlaneTexelFormat::kR8;
  int32_t width = 0;
  int32_t height = 0;
  int32_t bytes_per_texel = 0;

  [[nodiscard]] constexpr size_t RowBytes() const {
    return static_cast<size_t>(width) * static_cast<size_t>(bytes_per_texel);
  }
  [[nodiscard]] constexpr size_t ByteSize() const {
    return RowBytes() * static_cast<size_t>(height);
  }
};

struct PlaneLayout {
  size_t plane_count = 0;
  std::array<PlaneSpec, DecodedVideoFrame::kMaxPlanes> planes{};
  bool semi_planar = false;
  int bit_depth = 8;
  int32_t chroma_width = 0;
  int32_t chroma_height = 0;
};

constexpr int32_t SubsampledSize(int32_t size) { return (size + 1) / 2; }

std::optional<PlaneLayout> DescribePlanes(PixelFormat format, int32_t width,
                                          int32_t height);

}

#endif
