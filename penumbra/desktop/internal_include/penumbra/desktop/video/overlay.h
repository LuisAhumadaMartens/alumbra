#ifndef PENUMBRA_DESKTOP_VIDEO_OVERLAY_H_
#define PENUMBRA_DESKTOP_VIDEO_OVERLAY_H_

#include <SDL3/SDL_gpu.h>

#include "absl/status/status.h"
#include "penumbra/desktop/video/video_geometry.h"

namespace penumbra::desktop {

struct OverlayTarget {
  SDL_GPUTexture* texture = nullptr;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
  PixelSize pixels;
  float pixel_density = 1.0F;
};

class GpuOverlay {
 public:
  virtual ~GpuOverlay() = default;
  virtual absl::Status Prepare(SDL_GPUCommandBuffer* commands,
                               const OverlayTarget& target) = 0;
  virtual void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                    const OverlayTarget& target) = 0;
};

}

#endif
