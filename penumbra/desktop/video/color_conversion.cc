#include "penumbra/desktop/video/color_conversion.h"

#include <cstdint>
#include <optional>

#include "penumbra/desktop/video/plane_layout.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

constexpr uint8_t kMatrixBt709 = 1;
constexpr uint8_t kMatrixBt470Bg = 5;
constexpr uint8_t kMatrixSmpte170m = 6;
constexpr uint8_t kMatrixBt2020Ncl = 9;
constexpr int32_t kHighDefinitionWidth = 1280;
constexpr int32_t kHighDefinitionHeight = 720;
constexpr double kP010MaximumCode = 65535.0 / 64.0;
constexpr double kEightBitMaximumCode = 255.0;

}

MatrixStandard ResolveMatrixStandard(uint8_t matrix_code, int32_t width,
                                     int32_t height) {
  switch (matrix_code) {
    case kMatrixBt709:
      return MatrixStandard::kBt709;
    case kMatrixBt470Bg:
    case kMatrixSmpte170m:
      return MatrixStandard::kBt601;
    case kMatrixBt2020Ncl:
      return MatrixStandard::kBt2020;
    default:
      break;
  }
  if (width >= kHighDefinitionWidth || height >= kHighDefinitionHeight) {
    return MatrixStandard::kBt709;
  }
  return MatrixStandard::kBt601;
}

LumaWeights WeightsOf(MatrixStandard standard) {
  switch (standard) {
    case MatrixStandard::kBt601:
      return {.red = 0.299, .blue = 0.114};
    case MatrixStandard::kBt709:
      return {.red = 0.2126, .blue = 0.0722};
    case MatrixStandard::kBt2020:
      return {.red = 0.2627, .blue = 0.0593};
  }
  return {.red = 0.2126, .blue = 0.0722};
}

ChromaSiting ResolveChromaSiting(ChromaSiting siting) {
  if (siting == ChromaSiting::kUnspecified) {
    return ChromaSiting::kLeft;
  }
  return siting;
}

std::optional<ConversionParams> ComputeConversionParams(PixelFormat format,
                                                        const ColorSpace& color,
                                                        int32_t width,
                                                        int32_t height) {
  const std::optional<PlaneLayout> layout =
      DescribePlanes(format, width, height);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  const int shift = layout->bit_depth - 8;
  const double maximum_code =
      layout->bit_depth == 8 ? kEightBitMaximumCode : kP010MaximumCode;
  const auto code_range = static_cast<double>((1 << layout->bit_depth) - 1);
  const auto eight_bit_scale = static_cast<double>(1 << shift);

  double luma_scale = 0;
  double luma_bias = 0;
  double chroma_scale = 0;
  double chroma_bias = 0;
  if (color.full_range) {
    luma_scale = maximum_code / code_range;
    luma_bias = 0.0;
    chroma_scale = maximum_code / code_range;
    chroma_bias = -((128.0 * eight_bit_scale) / code_range);
  } else {
    luma_scale = maximum_code / (219.0 * eight_bit_scale);
    luma_bias = -16.0 / 219.0;
    chroma_scale = maximum_code / (224.0 * eight_bit_scale);
    chroma_bias = -128.0 / 224.0;
  }

  const LumaWeights weights =
      WeightsOf(ResolveMatrixStandard(color.matrix, width, height));
  const double green = 1.0 - weights.red - weights.blue;

  ConversionParams params;
  params.scale = {static_cast<float>(luma_scale),
                  static_cast<float>(chroma_scale),
                  static_cast<float>(chroma_scale), 0.0F};
  params.bias = {static_cast<float>(luma_bias), static_cast<float>(chroma_bias),
                 static_cast<float>(chroma_bias), 0.0F};
  params.matrix[0] = {1.0F, 0.0F, static_cast<float>(2.0 * (1.0 - weights.red)),
                      0.0F};
  params.matrix[1] = {
      1.0F,
      static_cast<float>(-2.0 * weights.blue * (1.0 - weights.blue) / green),
      static_cast<float>(-2.0 * weights.red * (1.0 - weights.red) / green),
      0.0F};
  params.matrix[2] = {1.0F, static_cast<float>(2.0 * (1.0 - weights.blue)),
                      0.0F, 0.0F};

  const ChromaSiting siting = ResolveChromaSiting(color.chroma_siting);
  const double shift_x = siting == ChromaSiting::kCenter ? 0.0 : 0.25;
  const double shift_y = siting == ChromaSiting::kTopLeft ? 0.25 : 0.0;
  const auto chroma_width = static_cast<double>(layout->chroma_width);
  const auto chroma_height = static_cast<double>(layout->chroma_height);
  params.chroma_transform = {static_cast<float>(width / (2.0 * chroma_width)),
                             static_cast<float>(height / (2.0 * chroma_height)),
                             static_cast<float>(shift_x / chroma_width),
                             static_cast<float>(shift_y / chroma_height)};
  params.layout_flags = {layout->semi_planar ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F};
  return params;
}

}
