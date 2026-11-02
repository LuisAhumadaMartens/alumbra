#ifndef PENUMBRA_DESKTOP_VIDEO_SHADER_BLOBS_H_
#define PENUMBRA_DESKTOP_VIDEO_SHADER_BLOBS_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace penumbra::desktop {

struct ShaderBinaries {
  std::span<const uint8_t> spirv;
  std::span<const uint8_t> msl;
};

const ShaderBinaries& FullscreenTriangleVertexShader();
const ShaderBinaries& YuvToRgbFragmentShader();

inline constexpr std::string_view kSpirvEntryPoint = "main";
inline constexpr std::string_view kMslEntryPoint = "main0";

}

#endif
