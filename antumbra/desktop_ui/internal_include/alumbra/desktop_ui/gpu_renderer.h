#ifndef ALUMBRA_DESKTOP_UI_GPU_RENDERER_H_
#define ALUMBRA_DESKTOP_UI_GPU_RENDERER_H_

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_rect.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/glyph_atlas.h"
#include "alumbra/desktop_ui/icons.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/overlay.h"

namespace alumbra::desktop_ui {

template <typename T, void (*Release)(SDL_GPUDevice*, T*)>
struct GpuDeleter {
  std::shared_ptr<penumbra::desktop::GpuContext> context;
  std::atomic<std::int64_t> penumbra::desktop::GpuResourceCounters::* counter =
      nullptr;

  void operator()(T* object) const {
    Release(context->device, object);
    (context->counters.*counter).fetch_sub(1);
  }
};

class UiGpuRenderer final : public penumbra::desktop::GpuOverlay {
 public:
  static absl::StatusOr<std::shared_ptr<UiGpuRenderer>> Create(
      std::shared_ptr<penumbra::desktop::GpuContext> context,
      std::shared_ptr<const FontFace> font);

  ~UiGpuRenderer() override;
  UiGpuRenderer(const UiGpuRenderer&) = delete;
  UiGpuRenderer& operator=(const UiGpuRenderer&) = delete;

  void SetDrawList(DrawList list) { draw_list_ = std::move(list); }

  absl::Status Prepare(SDL_GPUCommandBuffer* commands,
                       const penumbra::desktop::OverlayTarget& target) override;
  void Draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
            const penumbra::desktop::OverlayTarget& target) override;

  [[nodiscard]] std::size_t VertexCount() const { return vertices_.size(); }
  [[nodiscard]] std::size_t BatchCount() const { return batches_.size(); }
  [[nodiscard]] std::size_t AtlasGlyphCount() const {
    return atlas_.GlyphCount();
  }

 private:
  using TexturePtr =
      std::unique_ptr<SDL_GPUTexture,
                      GpuDeleter<SDL_GPUTexture, SDL_ReleaseGPUTexture>>;
  using BufferPtr =
      std::unique_ptr<SDL_GPUBuffer,
                      GpuDeleter<SDL_GPUBuffer, SDL_ReleaseGPUBuffer>>;
  using TransferPtr = std::unique_ptr<
      SDL_GPUTransferBuffer,
      GpuDeleter<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer>>;
  using ShaderPtr =
      std::unique_ptr<SDL_GPUShader,
                      GpuDeleter<SDL_GPUShader, SDL_ReleaseGPUShader>>;
  using PipelinePtr = std::unique_ptr<
      SDL_GPUGraphicsPipeline,
      GpuDeleter<SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline>>;
  using SamplerPtr =
      std::unique_ptr<SDL_GPUSampler,
                      GpuDeleter<SDL_GPUSampler, SDL_ReleaseGPUSampler>>;

  struct Vertex {
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
    float red = 0;
    float green = 0;
    float blue = 0;
    float alpha = 0;
    float half_width = 0;
    float half_height = 0;
    float radius = 0;
    float border_width = 0;
    float border_red = 0;
    float border_green = 0;
    float border_blue = 0;
    float border_alpha = 0;
    float local_x = 0;
    float local_y = 0;
    float kind = 0;
  };

  struct Batch {
    SDL_Rect scissor{};
    std::uint32_t first = 0;
    std::uint32_t count = 0;
  };

  struct Quad {
    float centre_x = 0;
    float centre_y = 0;
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
    float u0 = 0;
    float v0 = 0;
    float u1 = 0;
    float v1 = 0;
  };

  UiGpuRenderer(std::shared_ptr<penumbra::desktop::GpuContext> context,
                std::shared_ptr<const FontFace> font);

  absl::Status Initialize();
  absl::Status CreateShaders();
  absl::Status CreateSamplers();
  absl::StatusOr<TexturePtr> CreateAtlasTexture(int size);
  absl::StatusOr<TransferPtr> CreateTransfer(std::size_t size);
  absl::StatusOr<SDL_GPUGraphicsPipeline*> PipelineFor(
      SDL_GPUTextureFormat format);
  absl::Status EnsureVertexCapacity(std::size_t bytes);
  bool Build(const penumbra::desktop::OverlayTarget& target,
             bool allow_failure);
  void AppendBox(const RoundedRectCommand& box, float density);
  void AppendIcon(const IconCommand& icon, float density);
  bool AppendGlyph(const GlyphCommand& glyph, float density,
                   bool allow_failure);
  void AppendQuad(const Quad& quad, const Vertex& prototype);
  void CloseBatch(const SDL_Rect& scissor);
  absl::Status Upload(SDL_GPUCommandBuffer* commands);

  std::shared_ptr<penumbra::desktop::GpuContext> context_;
  std::shared_ptr<const FontFace> font_;
  GlyphAtlas atlas_;
  IconSdfAtlas icons_;
  DrawList draw_list_;
  std::vector<Vertex> vertices_;
  std::vector<Batch> batches_;
  std::uint32_t batch_start_ = 0;
  ShaderPtr vertex_shader_;
  ShaderPtr fragment_shader_;
  SamplerPtr sampler_;
  TexturePtr glyph_texture_;
  TexturePtr icon_texture_;
  TransferPtr atlas_transfer_;
  TransferPtr icon_transfer_;
  TransferPtr vertex_transfer_;
  BufferPtr vertex_buffer_;
  std::size_t vertex_capacity_ = 0;
  std::map<SDL_GPUTextureFormat, PipelinePtr> pipelines_;
  SDL_GPUGraphicsPipeline* current_pipeline_ = nullptr;
  bool icons_uploaded_ = false;
};

}

#endif
