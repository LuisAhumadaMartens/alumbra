#ifndef PENUMBRA_DESKTOP_VIDEO_VIDEO_GEOMETRY_H_
#define PENUMBRA_DESKTOP_VIDEO_VIDEO_GEOMETRY_H_

#include <cstdint>
#include <optional>

#include "penumbra/interface/rational.h"

namespace penumbra::desktop {

struct PixelSize {
  int32_t width = 0;
  int32_t height = 0;

  friend constexpr bool operator==(const PixelSize&,
                                   const PixelSize&) = default;
};

struct PixelRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  friend constexpr bool operator==(const PixelRect&,
                                   const PixelRect&) = default;
};

struct DisplayAspect {
  int64_t numerator = 1;
  int64_t denominator = 1;
};

std::optional<DisplayAspect> ComputeDisplayAspect(PixelSize visible,
                                                  Rational sample_aspect_ratio);

PixelRect FitInside(PixelRect bounds, DisplayAspect aspect);

PixelSize ScaleToPixels(PixelSize logical, float pixel_density);

}

#endif
