#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/plane_layout.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "test_frames.h"

namespace penumbra::desktop {
namespace {

using testing::Quantize;
using testing::Rgb;
using testing::Yuv;

std::array<double, 3> Apply(const ConversionParams& params, double luma,
                            double blue, double red) {
  const std::array<double, 3> raw = {luma, blue, red};
  std::array<double, 3> yuv{};
  for (size_t index = 0; index < 3; ++index) {
    yuv.at(index) =
        (raw.at(index) * params.scale.at(index)) + params.bias.at(index);
  }
  std::array<double, 3> rgb{};
  for (size_t row = 0; row < 3; ++row) {
    const std::array<float, 4>& weights = params.matrix.at(row);
    rgb.at(row) = (weights.at(0) * yuv.at(0)) + (weights.at(1) * yuv.at(1)) +
                  (weights.at(2) * yuv.at(2));
  }
  return rgb;
}

double Normalized(uint16_t code, int bit_depth) {
  if (bit_depth == 8) {
    return code / 255.0;
  }
  return (code * 64.0) / 65535.0;
}

TEST(MatrixStandardTest, FollowsH273CodePoints) {
  EXPECT_EQ(ResolveMatrixStandard(1, 640, 360), MatrixStandard::kBt709);
  EXPECT_EQ(ResolveMatrixStandard(5, 1920, 1080), MatrixStandard::kBt601);
  EXPECT_EQ(ResolveMatrixStandard(6, 1920, 1080), MatrixStandard::kBt601);
  EXPECT_EQ(ResolveMatrixStandard(9, 640, 360), MatrixStandard::kBt2020);
}

TEST(MatrixStandardTest, GuessesFromResolutionWhenUnspecified) {
  EXPECT_EQ(ResolveMatrixStandard(ColorSpace::kUnspecified, 720, 576),
            MatrixStandard::kBt601);
  EXPECT_EQ(ResolveMatrixStandard(ColorSpace::kUnspecified, 1280, 720),
            MatrixStandard::kBt709);
  EXPECT_EQ(ResolveMatrixStandard(ColorSpace::kUnspecified, 3840, 2160),
            MatrixStandard::kBt709);
}

TEST(PlaneLayoutTest, DescribesEachCpuFormat) {
  const PlaneLayout i420 = *DescribePlanes(PixelFormat::kI420, 5, 3);
  EXPECT_EQ(i420.plane_count, 3U);
  EXPECT_EQ(i420.planes[1].width, 3);
  EXPECT_EQ(i420.planes[1].height, 2);
  EXPECT_FALSE(i420.semi_planar);

  const PlaneLayout nv12 = *DescribePlanes(PixelFormat::kNv12, 16, 8);
  EXPECT_EQ(nv12.plane_count, 2U);
  EXPECT_EQ(nv12.planes[1].format, PlaneTexelFormat::kR8G8);
  EXPECT_EQ(nv12.planes[1].RowBytes(), 16U);
  EXPECT_TRUE(nv12.semi_planar);

  const PlaneLayout p010 = *DescribePlanes(PixelFormat::kP010, 16, 8);
  EXPECT_EQ(p010.planes[0].format, PlaneTexelFormat::kR16);
  EXPECT_EQ(p010.planes[1].format, PlaneTexelFormat::kR16G16);
  EXPECT_EQ(p010.planes[1].bytes_per_texel, 4);
  EXPECT_EQ(p010.bit_depth, 10);
}

TEST(PlaneLayoutTest, RejectsUnsupportedFormatsAndSizes) {
  EXPECT_FALSE(DescribePlanes(PixelFormat::kOpaque, 16, 16).has_value());
  EXPECT_FALSE(DescribePlanes(PixelFormat::kUnknown, 16, 16).has_value());
  EXPECT_FALSE(DescribePlanes(PixelFormat::kI420, 0, 16).has_value());
}

struct ConversionCase {
  PixelFormat format;
  MatrixStandard standard;
  uint8_t matrix_code;
  bool full_range;
};

class ConversionParamsTest : public ::testing::TestWithParam<ConversionCase> {};

TEST_P(ConversionParamsTest, DecodesEncodedColourBars) {
  const ConversionCase& test = GetParam();
  ColorSpace color;
  color.matrix = test.matrix_code;
  color.full_range = test.full_range;
  const std::optional<ConversionParams> params =
      ComputeConversionParams(test.format, color, 64, 64);
  ASSERT_TRUE(params.has_value());
  const int bit_depth = test.format == PixelFormat::kP010 ? 10 : 8;
  const double tolerance = bit_depth == 8 ? 0.012 : 0.004;
  for (const Rgb& bar : testing::ColourBars()) {
    const Yuv yuv = testing::Encode(bar, test.standard);
    const std::array<double, 3> rgb = Apply(
        *params,
        Normalized(Quantize(yuv.luma, bit_depth, false, test.full_range),
                   bit_depth),
        Normalized(
            Quantize(yuv.blue_difference, bit_depth, true, test.full_range),
            bit_depth),
        Normalized(
            Quantize(yuv.red_difference, bit_depth, true, test.full_range),
            bit_depth));
    EXPECT_NEAR(std::clamp(rgb.at(0), 0.0, 1.0), bar.red, tolerance);
    EXPECT_NEAR(std::clamp(rgb.at(1), 0.0, 1.0), bar.green, tolerance);
    EXPECT_NEAR(std::clamp(rgb.at(2), 0.0, 1.0), bar.blue, tolerance);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllFormatsMatricesAndRanges, ConversionParamsTest,
    ::testing::Values(
        ConversionCase{PixelFormat::kI420, MatrixStandard::kBt601, 6, false},
        ConversionCase{PixelFormat::kI420, MatrixStandard::kBt601, 6, true},
        ConversionCase{PixelFormat::kI420, MatrixStandard::kBt709, 1, false},
        ConversionCase{PixelFormat::kI420, MatrixStandard::kBt709, 1, true},
        ConversionCase{PixelFormat::kNv12, MatrixStandard::kBt709, 1, false},
        ConversionCase{PixelFormat::kNv12, MatrixStandard::kBt601, 5, true},
        ConversionCase{PixelFormat::kP010, MatrixStandard::kBt709, 1, false},
        ConversionCase{PixelFormat::kP010, MatrixStandard::kBt709, 1, true},
        ConversionCase{PixelFormat::kP010, MatrixStandard::kBt2020, 9, false}));

TEST(ConversionParamsTest, MatricesDifferBetweenStandards) {
  ColorSpace bt601;
  bt601.matrix = 6;
  ColorSpace bt709;
  bt709.matrix = 1;
  const auto a = ComputeConversionParams(PixelFormat::kI420, bt601, 64, 64);
  const auto b = ComputeConversionParams(PixelFormat::kI420, bt709, 64, 64);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_GT(std::abs(a->matrix.at(1).at(1) - b->matrix.at(1).at(1)), 0.05F);
  EXPECT_GT(std::abs(a->matrix.at(0).at(2) - b->matrix.at(0).at(2)), 0.05F);
}

TEST(ConversionParamsTest, SitingShiftsChromaCoordinates) {
  const auto transform = [](ChromaSiting siting) {
    ColorSpace color;
    color.chroma_siting = siting;
    return ComputeConversionParams(PixelFormat::kI420, color, 64, 32)
        ->chroma_transform;
  };
  const auto left = transform(ChromaSiting::kLeft);
  const auto center = transform(ChromaSiting::kCenter);
  const auto top_left = transform(ChromaSiting::kTopLeft);
  const auto unspecified = transform(ChromaSiting::kUnspecified);
  EXPECT_FLOAT_EQ(left[2], 0.25F / 32.0F);
  EXPECT_FLOAT_EQ(left[3], 0.0F);
  EXPECT_FLOAT_EQ(center[2], 0.0F);
  EXPECT_FLOAT_EQ(center[3], 0.0F);
  EXPECT_FLOAT_EQ(top_left[2], 0.25F / 32.0F);
  EXPECT_FLOAT_EQ(top_left[3], 0.25F / 16.0F);
  EXPECT_EQ(unspecified, left);
}

TEST(ConversionParamsTest, RejectsFormatsWithoutPlanes) {
  EXPECT_FALSE(
      ComputeConversionParams(PixelFormat::kOpaque, ColorSpace{}, 16, 16)
          .has_value());
}

TEST(DisplayAspectTest, AppliesSampleAspectRatio) {
  const auto pal =
      ComputeDisplayAspect(testing::Size(720, 576), Rational::Of(16, 15));
  ASSERT_TRUE(pal.has_value());
  EXPECT_EQ(pal->numerator, 4);
  EXPECT_EQ(pal->denominator, 3);
  const auto square =
      ComputeDisplayAspect(testing::Size(1920, 1080), Rational::Of(1, 1));
  ASSERT_TRUE(square.has_value());
  EXPECT_EQ(square->numerator, 16);
  EXPECT_EQ(square->denominator, 9);
  const auto narrow =
      ComputeDisplayAspect(testing::Size(720, 480), Rational::Of(8, 9));
  ASSERT_TRUE(narrow.has_value());
  EXPECT_EQ(narrow->numerator, 4);
  EXPECT_EQ(narrow->denominator, 3);
}

TEST(DisplayAspectTest, RejectsInvalidInput) {
  EXPECT_FALSE(ComputeDisplayAspect(testing::Size(0, 480), Rational::Of(1, 1))
                   .has_value());
  EXPECT_FALSE(
      ComputeDisplayAspect(testing::Size(720, 480), Rational()).has_value());
}

TEST(FitInsideTest, LetterboxesAndPillarboxes) {
  const DisplayAspect wide = testing::Aspect(16, 9);
  EXPECT_EQ(FitInside(testing::Rect(0, 0, 800, 800), wide),
            (testing::Rect(0, 175, 800, 450)));
  EXPECT_EQ(FitInside(testing::Rect(0, 0, 1600, 600), wide),
            (testing::Rect(266, 0, 1067, 600)));
  EXPECT_EQ(FitInside(testing::Rect(0, 0, 1600, 900), wide),
            (testing::Rect(0, 0, 1600, 900)));
  EXPECT_EQ(FitInside(testing::Rect(10, 20, 640, 480), testing::Aspect(4, 3)),
            (testing::Rect(10, 20, 640, 480)));
}

TEST(FitInsideTest, KeepsAtLeastOnePixel) {
  const PixelRect tiny =
      FitInside(testing::Rect(0, 0, 100, 1), testing::Aspect(16, 9));
  EXPECT_GE(tiny.width, 1);
  EXPECT_GE(tiny.height, 1);
  EXPECT_EQ(FitInside(testing::Rect(0, 0, 0, 10), testing::Aspect(1, 1)).width,
            0);
}

TEST(PixelDensityTest, ScalesLogicalSizeToPixels) {
  EXPECT_EQ(ScaleToPixels(testing::Size(640, 360), 2.0F),
            (testing::Size(1280, 720)));
  EXPECT_EQ(ScaleToPixels(testing::Size(641, 361), 1.5F),
            (testing::Size(962, 542)));
  EXPECT_EQ(ScaleToPixels(testing::Size(640, 360), 0.0F), (PixelSize{}));
}

}
}
