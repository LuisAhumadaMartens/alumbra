#include "umbra/codec/video_geometry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::codec {
namespace {

constexpr std::array<std::pair<std::int64_t, std::int64_t>, 16> kIndicators{{
    {1, 1},
    {12, 11},
    {10, 11},
    {16, 11},
    {40, 33},
    {24, 11},
    {20, 11},
    {32, 11},
    {80, 33},
    {18, 11},
    {15, 11},
    {64, 33},
    {160, 99},
    {4, 3},
    {3, 2},
    {2, 1},
}};

}

penumbra::Rational AspectRatioFromIndicator(std::uint32_t indicator) {
  if (indicator == 0 || indicator > kIndicators.size()) {
    return penumbra::Rational::Of(1, 1);
  }
  const auto [numerator, denominator] = kIndicators.at(indicator - 1);
  return penumbra::Rational::Create(numerator, denominator)
      .value_or(penumbra::Rational::Of(1, 1));
}

std::int32_t ScaleByAspectRatio(std::int32_t visible_width,
                                const penumbra::Rational& sample_aspect_ratio) {
  const std::int64_t numerator = sample_aspect_ratio.numerator();
  const std::int64_t denominator = sample_aspect_ratio.denominator();
  if (numerator <= 0 || denominator <= 0) {
    return visible_width;
  }
  const std::int64_t scaled =
      (static_cast<std::int64_t>(visible_width) * numerator + denominator / 2) /
      denominator;
  return static_cast<std::int32_t>(
      std::clamp<std::int64_t>(scaled, 1, INT32_MAX));
}

penumbra::VideoInfo MakeVideoInfo(std::int32_t coded_width,
                                  std::int32_t coded_height,
                                  std::int32_t visible_width,
                                  std::int32_t visible_height,
                                  const penumbra::Rational& sample_aspect_ratio,
                                  const penumbra::ColorSpace& color) {
  penumbra::VideoInfo info;
  info.coded_width = coded_width;
  info.coded_height = coded_height;
  info.display_width = ScaleByAspectRatio(visible_width, sample_aspect_ratio);
  info.display_height = visible_height;
  info.sample_aspect_ratio = sample_aspect_ratio;
  info.color = color;
  return info;
}

}
