#include "penumbra/desktop/video/video_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>

#include "penumbra/interface/rational.h"

namespace penumbra::desktop {
namespace {

constexpr int64_t kMaxDimension = std::numeric_limits<int32_t>::max();
constexpr int64_t kMaxSampleAspectTerm = std::numeric_limits<int32_t>::max();

}

std::optional<DisplayAspect> ComputeDisplayAspect(
    PixelSize visible, Rational sample_aspect_ratio) {
  if (visible.width <= 0 || visible.height <= 0 ||
      !sample_aspect_ratio.is_positive() ||
      sample_aspect_ratio.numerator() > kMaxSampleAspectTerm ||
      sample_aspect_ratio.denominator() > kMaxSampleAspectTerm) {
    return std::nullopt;
  }
  const int64_t numerator =
      static_cast<int64_t>(visible.width) * sample_aspect_ratio.numerator();
  const int64_t denominator =
      static_cast<int64_t>(visible.height) * sample_aspect_ratio.denominator();
  const int64_t divisor = std::gcd(numerator, denominator);
  return DisplayAspect{.numerator = numerator / divisor,
                       .denominator = denominator / divisor};
}

PixelRect FitInside(PixelRect bounds, DisplayAspect aspect) {
  if (bounds.width <= 0 || bounds.height <= 0 || aspect.numerator <= 0 ||
      aspect.denominator <= 0) {
    return PixelRect{.x = bounds.x, .y = bounds.y, .width = 0, .height = 0};
  }
  const double ratio = static_cast<double>(aspect.numerator) /
                       static_cast<double>(aspect.denominator);
  double width = bounds.width;
  double height = bounds.height;
  if (width / height <= ratio) {
    height = width / ratio;
  } else {
    width = height * ratio;
  }
  const int32_t fitted_width = static_cast<int32_t>(
      std::clamp<double>(std::round(width), 1.0, bounds.width));
  const int32_t fitted_height = static_cast<int32_t>(
      std::clamp<double>(std::round(height), 1.0, bounds.height));
  return PixelRect{.x = bounds.x + ((bounds.width - fitted_width) / 2),
                   .y = bounds.y + ((bounds.height - fitted_height) / 2),
                   .width = fitted_width,
                   .height = fitted_height};
}

PixelSize ScaleToPixels(PixelSize logical, float pixel_density) {
  if (!(pixel_density > 0.0F) || logical.width <= 0 || logical.height <= 0) {
    return PixelSize{};
  }
  const auto scale = [pixel_density](int32_t value) {
    const double scaled = std::round(static_cast<double>(value) *
                                     static_cast<double>(pixel_density));
    return static_cast<int32_t>(
        std::clamp<double>(scaled, 1.0, static_cast<double>(kMaxDimension)));
  };
  return PixelSize{.width = scale(logical.width),
                   .height = scale(logical.height)};
}

}
