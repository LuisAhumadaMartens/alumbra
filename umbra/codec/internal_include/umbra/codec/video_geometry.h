#ifndef UMBRA_CODEC_VIDEO_GEOMETRY_H_
#define UMBRA_CODEC_VIDEO_GEOMETRY_H_

#include <cstdint>

#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::codec {

struct CropRect {
  std::uint32_t left = 0;
  std::uint32_t right = 0;
  std::uint32_t top = 0;
  std::uint32_t bottom = 0;
};

[[nodiscard]] std::int32_t ScaleByAspectRatio(
    std::int32_t visible_width, const penumbra::Rational& sample_aspect_ratio);

[[nodiscard]] penumbra::Rational AspectRatioFromIndicator(
    std::uint32_t indicator);

[[nodiscard]] penumbra::VideoInfo MakeVideoInfo(
    std::int32_t coded_width, std::int32_t coded_height,
    std::int32_t visible_width, std::int32_t visible_height,
    const penumbra::Rational& sample_aspect_ratio,
    const penumbra::ColorSpace& color);

}

#endif
