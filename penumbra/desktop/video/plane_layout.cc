#include "penumbra/desktop/video/plane_layout.h"

#include <cstdint>
#include <optional>

#include "penumbra/interface/decoded_frame.h"

namespace penumbra::desktop {
namespace {

constexpr PlaneSpec Plane(PlaneTexelFormat format, int32_t width,
                          int32_t height, int32_t bytes_per_texel) {
  return PlaneSpec{.format = format,
                   .width = width,
                   .height = height,
                   .bytes_per_texel = bytes_per_texel};
}

}

std::optional<PlaneLayout> DescribePlanes(PixelFormat format, int32_t width,
                                          int32_t height) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  PlaneLayout layout;
  layout.chroma_width = SubsampledSize(width);
  layout.chroma_height = SubsampledSize(height);
  switch (format) {
    case PixelFormat::kI420:
      layout.plane_count = 3;
      layout.planes[0] = Plane(PlaneTexelFormat::kR8, width, height, 1);
      layout.planes[1] = Plane(PlaneTexelFormat::kR8, layout.chroma_width,
                               layout.chroma_height, 1);
      layout.planes[2] = layout.planes[1];
      layout.bit_depth = 8;
      return layout;
    case PixelFormat::kNv12:
      layout.plane_count = 2;
      layout.semi_planar = true;
      layout.planes[0] = Plane(PlaneTexelFormat::kR8, width, height, 1);
      layout.planes[1] = Plane(PlaneTexelFormat::kR8G8, layout.chroma_width,
                               layout.chroma_height, 2);
      layout.bit_depth = 8;
      return layout;
    case PixelFormat::kP010:
      layout.plane_count = 2;
      layout.semi_planar = true;
      layout.planes[0] = Plane(PlaneTexelFormat::kR16, width, height, 2);
      layout.planes[1] = Plane(PlaneTexelFormat::kR16G16, layout.chroma_width,
                               layout.chroma_height, 4);
      layout.bit_depth = 10;
      return layout;
    case PixelFormat::kUnknown:
    case PixelFormat::kOpaque:
      return std::nullopt;
  }
  return std::nullopt;
}

}
