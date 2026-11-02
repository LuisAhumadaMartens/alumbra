#ifndef TEST_PENUMBRA_DESKTOP_TEST_FRAMES_H_
#define TEST_PENUMBRA_DESKTOP_TEST_FRAMES_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/plane_layout.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop::testing {

inline PixelSize Size(int32_t width, int32_t height) {
  return PixelSize{.width = width, .height = height};
}

inline PixelRect Rect(int32_t x, int32_t y, int32_t width, int32_t height) {
  return PixelRect{.x = x, .y = y, .width = width, .height = height};
}

inline DisplayAspect Aspect(int64_t numerator, int64_t denominator) {
  return DisplayAspect{.numerator = numerator, .denominator = denominator};
}

struct Rgb {
  double red = 0;
  double green = 0;
  double blue = 0;
};

struct Yuv {
  double luma = 0;
  double blue_difference = 0;
  double red_difference = 0;
};

inline Yuv Encode(const Rgb& rgb, MatrixStandard standard) {
  const LumaWeights weights = WeightsOf(standard);
  const double green_weight = 1.0 - weights.red - weights.blue;
  const double luma = (weights.red * rgb.red) + (green_weight * rgb.green) +
                      (weights.blue * rgb.blue);
  return Yuv{
      .luma = luma,
      .blue_difference = (rgb.blue - luma) / (2.0 * (1.0 - weights.blue)),
      .red_difference = (rgb.red - luma) / (2.0 * (1.0 - weights.red))};
}

inline uint16_t Quantize(double value, int bit_depth, bool chroma,
                         bool full_range) {
  const auto scale = static_cast<double>(1 << (bit_depth - 8));
  const auto span = static_cast<double>((1 << bit_depth) - 1);
  double code = 0;
  if (full_range) {
    code = chroma ? ((128.0 * scale) + (value * span)) : (value * span);
  } else if (chroma) {
    code = (128.0 * scale) + (value * 224.0 * scale);
  } else {
    code = (16.0 * scale) + (value * 219.0 * scale);
  }
  return static_cast<uint16_t>(std::lround(std::clamp(code, 0.0, span)));
}

class TestFrame {
 public:
  TestFrame(PixelFormat format, int32_t width, int32_t height,
            const ColorSpace& color)
      : layout_(*DescribePlanes(format, width, height)) {
    frame_.format = format;
    frame_.width = width;
    frame_.height = height;
    frame_.color = color;
    for (size_t index = 0; index < layout_.plane_count; ++index) {
      storage_.at(index).assign(layout_.planes.at(index).ByteSize(), 0);
      frame_.planes.at(index).data = storage_.at(index).data();
      frame_.planes.at(index).stride =
          static_cast<int32_t>(layout_.planes.at(index).RowBytes());
    }
  }

  void SetLuma(int32_t x, int32_t y, uint16_t code) {
    if (layout_.bit_depth == 8) {
      storage_.at(0).at(Offset(0, x, y)) = static_cast<uint8_t>(code);
    } else {
      Store16(0, x, y, 0, static_cast<uint16_t>(code << 6));
    }
  }

  void SetChroma(int32_t x, int32_t y, uint16_t blue_code, uint16_t red_code) {
    if (layout_.bit_depth == 8 && layout_.semi_planar) {
      storage_.at(1).at(Offset(1, x, y)) = static_cast<uint8_t>(blue_code);
      storage_.at(1).at(Offset(1, x, y) + 1) = static_cast<uint8_t>(red_code);
    } else if (layout_.bit_depth == 8) {
      storage_.at(1).at(Offset(1, x, y)) = static_cast<uint8_t>(blue_code);
      storage_.at(2).at(Offset(2, x, y)) = static_cast<uint8_t>(red_code);
    } else {
      Store16(1, x, y, 0, static_cast<uint16_t>(blue_code << 6));
      Store16(1, x, y, 1, static_cast<uint16_t>(red_code << 6));
    }
  }

  [[nodiscard]] uint16_t LumaCode(int32_t x, int32_t y) const {
    if (layout_.bit_depth == 8) {
      return storage_.at(0).at(Offset(0, x, y));
    }
    return static_cast<uint16_t>(Load16(0, x, y, 0) >> 6);
  }

  [[nodiscard]] std::array<uint16_t, 2> ChromaCodes(int32_t x,
                                                    int32_t y) const {
    if (layout_.bit_depth == 8 && layout_.semi_planar) {
      return {storage_.at(1).at(Offset(1, x, y)),
              storage_.at(1).at(Offset(1, x, y) + 1)};
    }
    if (layout_.bit_depth == 8) {
      return {storage_.at(1).at(Offset(1, x, y)),
              storage_.at(2).at(Offset(2, x, y))};
    }
    return {static_cast<uint16_t>(Load16(1, x, y, 0) >> 6),
            static_cast<uint16_t>(Load16(1, x, y, 1) >> 6)};
  }

  [[nodiscard]] const DecodedVideoFrame& Frame() const { return frame_; }
  [[nodiscard]] DecodedVideoFrame& Frame() { return frame_; }
  [[nodiscard]] const PlaneLayout& Layout() const { return layout_; }

 private:
  [[nodiscard]] size_t Offset(size_t plane, int32_t x, int32_t y) const {
    const PlaneSpec& spec = layout_.planes.at(plane);
    return (static_cast<size_t>(y) * spec.RowBytes()) +
           (static_cast<size_t>(x) * static_cast<size_t>(spec.bytes_per_texel));
  }

  [[nodiscard]] uint16_t Load16(size_t plane, int32_t x, int32_t y,
                                size_t component) const {
    const size_t offset = Offset(plane, x, y) + (component * 2);
    const std::vector<uint8_t>& bytes = storage_.at(plane);
    return static_cast<uint16_t>(bytes.at(offset) |
                                 (bytes.at(offset + 1) << 8));
  }

  void Store16(size_t plane, int32_t x, int32_t y, size_t component,
               uint16_t value) {
    const size_t offset = Offset(plane, x, y) + (component * 2);
    std::vector<uint8_t>& bytes = storage_.at(plane);
    bytes.at(offset) = static_cast<uint8_t>(value & 0xFFU);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 8);
  }

  PlaneLayout layout_;
  DecodedVideoFrame frame_;
  std::array<std::vector<uint8_t>, DecodedVideoFrame::kMaxPlanes> storage_;
};

inline const std::array<Rgb, 8>& ColourBars() {
  static const std::array<Rgb, 8> kBars = {{
      {.red = 1.0, .green = 1.0, .blue = 1.0},
      {.red = 1.0, .green = 1.0, .blue = 0.0},
      {.red = 0.0, .green = 1.0, .blue = 1.0},
      {.red = 0.0, .green = 1.0, .blue = 0.0},
      {.red = 1.0, .green = 0.0, .blue = 1.0},
      {.red = 1.0, .green = 0.0, .blue = 0.0},
      {.red = 0.0, .green = 0.0, .blue = 1.0},
      {.red = 0.0, .green = 0.0, .blue = 0.0},
  }};
  return kBars;
}

inline TestFrame MakeColourBarFrame(PixelFormat format, int32_t width,
                                    int32_t height, const ColorSpace& color,
                                    MatrixStandard encoded_with) {
  TestFrame frame(format, width, height, color);
  const int bit_depth = frame.Layout().bit_depth;
  const int32_t bar_width = width / 8;
  const auto bar_at = [bar_width](int32_t x) -> const Rgb& {
    return ColourBars().at(static_cast<size_t>(x / bar_width));
  };
  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      const Yuv yuv = Encode(bar_at(x), encoded_with);
      frame.SetLuma(x, y,
                    Quantize(yuv.luma, bit_depth, false, color.full_range));
    }
  }
  for (int32_t y = 0; y < frame.Layout().chroma_height; ++y) {
    for (int32_t x = 0; x < frame.Layout().chroma_width; ++x) {
      const Yuv yuv = Encode(bar_at(std::min(2 * x, width - 1)), encoded_with);
      frame.SetChroma(
          x, y,
          Quantize(yuv.blue_difference, bit_depth, true, color.full_range),
          Quantize(yuv.red_difference, bit_depth, true, color.full_range));
    }
  }
  return frame;
}

}

#endif
