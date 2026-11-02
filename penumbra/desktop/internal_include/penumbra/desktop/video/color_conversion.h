#ifndef PENUMBRA_DESKTOP_VIDEO_COLOR_CONVERSION_H_
#define PENUMBRA_DESKTOP_VIDEO_COLOR_CONVERSION_H_

#include <array>
#include <cstdint>
#include <optional>

#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

enum class MatrixStandard : uint8_t {
  kBt601,
  kBt709,
  kBt2020,
};

struct LumaWeights {
  double red;
  double blue;
};

struct ConversionParams {
  std::array<float, 4> scale{};
  std::array<float, 4> bias{};
  std::array<std::array<float, 4>, 3> matrix{};
  std::array<float, 4> chroma_transform{};
  std::array<float, 4> layout_flags{};
};

static_assert(sizeof(ConversionParams) == 28 * sizeof(float));

MatrixStandard ResolveMatrixStandard(uint8_t matrix_code, int32_t width,
                                     int32_t height);

LumaWeights WeightsOf(MatrixStandard standard);

ChromaSiting ResolveChromaSiting(ChromaSiting siting);

std::optional<ConversionParams> ComputeConversionParams(PixelFormat format,
                                                        const ColorSpace& color,
                                                        int32_t width,
                                                        int32_t height);

}

#endif
