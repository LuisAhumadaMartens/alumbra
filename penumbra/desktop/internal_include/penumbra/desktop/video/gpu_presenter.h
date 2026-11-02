#ifndef PENUMBRA_DESKTOP_VIDEO_GPU_PRESENTER_H_
#define PENUMBRA_DESKTOP_VIDEO_GPU_PRESENTER_H_

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/plane_layout.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

struct GpuResourceCounters {
  std::atomic<int64_t> textures{0};
  std::atomic<int64_t> transfer_buffers{0};
  std::atomic<int64_t> shaders{0};
  std::atomic<int64_t> pipelines{0};
  std::atomic<int64_t> samplers{0};
  std::atomic<int64_t> fences{0};

  [[nodiscard]] int64_t Live() const {
    return textures.load() + transfer_buffers.load() + shaders.load() +
           pipelines.load() + samplers.load() + fences.load();
  }
};

struct GpuContext {
  GpuContext() = default;
  ~GpuContext();
  GpuContext(const GpuContext&) = delete;
  GpuContext& operator=(const GpuContext&) = delete;

  SDL_GPUDevice* device = nullptr;
  SDL_Window* claimed_window = nullptr;
  GpuResourceCounters counters;
};

using SurfaceTextures =
    std::array<SDL_GPUTexture*, DecodedVideoFrame::kMaxPlanes>;

class TexturePlanesSurface final : public GpuSurfaceHandle {
 public:
  TexturePlanesSurface(std::shared_ptr<GpuContext> context,
                       PixelFormat pixel_format, PixelSize size,
                       SurfaceTextures textures)
      : context_(std::move(context)),
        pixel_format_(pixel_format),
        size_(size),
        textures_(textures) {}
  ~TexturePlanesSurface() override;
  TexturePlanesSurface(const TexturePlanesSurface&) = delete;
  TexturePlanesSurface& operator=(const TexturePlanesSurface&) = delete;

  [[nodiscard]] SurfaceKind Kind() const override {
    return SurfaceKind::kTexturePlanes;
  }
  [[nodiscard]] const GpuContext* Context() const { return context_.get(); }
  [[nodiscard]] PixelFormat Format() const { return pixel_format_; }
  [[nodiscard]] PixelSize Size() const { return size_; }
  [[nodiscard]] const SurfaceTextures& Textures() const { return textures_; }

 private:
  std::shared_ptr<GpuContext> context_;
  PixelFormat pixel_format_;
  PixelSize size_;
  SurfaceTextures textures_;
};

struct GpuPresenterOptions {
  SDL_Window* window = nullptr;
  PixelSize offscreen_logical_size{.width = 640, .height = 360};
  float offscreen_pixel_density = 1.0F;
  ScalingFilter scaling_filter = ScalingFilter::kLinear;
  uint32_t frames_in_flight = 1;
  bool debug_mode = false;
};

class GpuPresenter final : public Presenter {
 public:
  static absl::StatusOr<std::unique_ptr<GpuPresenter>> Create(
      const GpuPresenterOptions& options);

  ~GpuPresenter() override;
  GpuPresenter(const GpuPresenter&) = delete;
  GpuPresenter& operator=(const GpuPresenter&) = delete;

  absl::Status Present(const PresentRequest& request) override;
  absl::Status SetScalingFilter(ScalingFilter filter) override;
  void SetObserver(std::shared_ptr<PresenterObserver> observer) override;
  [[nodiscard]] RefreshRate CurrentRefreshRate() const override;
  [[nodiscard]] PresenterStats Stats() const override;
  [[nodiscard]] PresenterCapabilities Capabilities() const override;

  bool HandleEvent(const SDL_Event& event);

  absl::Status ResizeOffscreen(PixelSize logical_size, float pixel_density);
  absl::StatusOr<RgbaImage> ReadbackTarget();
  absl::StatusOr<std::shared_ptr<const GpuSurfaceHandle>> CreateTestSurface(
      const DecodedVideoFrame& frame);

  [[nodiscard]] std::shared_ptr<GpuContext> Context() const { return context_; }
  [[nodiscard]] PixelSize TargetPixels() const { return target_pixels_; }
  [[nodiscard]] float PixelDensity() const { return pixel_density_; }
  [[nodiscard]] std::string DriverName() const;

 private:
  struct TextureDeleter {
    std::shared_ptr<GpuContext> context;
    void operator()(SDL_GPUTexture* texture) const;
  };
  struct TransferBufferDeleter {
    std::shared_ptr<GpuContext> context;
    void operator()(SDL_GPUTransferBuffer* buffer) const;
  };
  struct ShaderDeleter {
    std::shared_ptr<GpuContext> context;
    void operator()(SDL_GPUShader* shader) const;
  };
  struct PipelineDeleter {
    std::shared_ptr<GpuContext> context;
    void operator()(SDL_GPUGraphicsPipeline* pipeline) const;
  };
  struct SamplerDeleter {
    std::shared_ptr<GpuContext> context;
    void operator()(SDL_GPUSampler* sampler) const;
  };

  using TexturePtr = std::unique_ptr<SDL_GPUTexture, TextureDeleter>;
  using TransferBufferPtr =
      std::unique_ptr<SDL_GPUTransferBuffer, TransferBufferDeleter>;
  using ShaderPtr = std::unique_ptr<SDL_GPUShader, ShaderDeleter>;
  using PipelinePtr = std::unique_ptr<SDL_GPUGraphicsPipeline, PipelineDeleter>;
  using SamplerPtr = std::unique_ptr<SDL_GPUSampler, SamplerDeleter>;

  struct PlaneResource {
    PlaneSpec spec;
    TexturePtr texture;
    TransferBufferPtr upload;
  };

  struct PlaneTextures {
    PixelFormat format = PixelFormat::kUnknown;
    PixelSize size;
    std::vector<PlaneResource> planes;
  };

  struct BoundFrame {
    PixelFormat format = PixelFormat::kUnknown;
    PixelSize size;
    ColorSpace color;
    SurfaceTextures textures{};
    std::shared_ptr<const void> keep_alive;
  };

  struct TargetSurface {
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    PixelSize size;
  };

  GpuPresenter(std::shared_ptr<GpuContext> context,
               const GpuPresenterOptions& options);

  absl::Status Initialize();
  absl::Status CreateShaders();
  absl::Status CreateSamplers();
  absl::StatusOr<SDL_GPUGraphicsPipeline*> PipelineFor(
      SDL_GPUTextureFormat format);
  absl::StatusOr<TexturePtr> CreatePlaneTexture(const PlaneSpec& spec);
  absl::StatusOr<TransferBufferPtr> CreateTransferBuffer(
      SDL_GPUTransferBufferUsage usage, size_t size);
  absl::StatusOr<TexturePtr> CreateRenderTarget(PixelSize size);
  absl::Status PrepareCpuFrame(const DecodedVideoFrame& frame);
  absl::Status UploadPlanes(SDL_GPUCommandBuffer* commands,
                            const DecodedVideoFrame& frame);
  absl::Status BindOpaqueFrame(const DecodedVideoFrame& frame);
  absl::Status BindNewFrame(const DecodedVideoFrame& frame);
  absl::StatusOr<TargetSurface> AcquireTarget(SDL_GPUCommandBuffer* commands);
  absl::StatusOr<PixelRect> RecordVideoPass(SDL_GPUCommandBuffer* commands,
                                            const TargetSurface& target,
                                            const PresentRequest& request);
  void DrawVideo(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands,
                 const BoundFrame& frame, const ConversionParams& params,
                 SDL_GPUGraphicsPipeline* pipeline,
                 const PixelRect& video_rect);
  void RecordPresented(const PresentRequest& request, int64_t acquire_ticks,
                       int64_t submit_ticks, const PixelRect& video_rect);
  void CountDrop(const PresentRequest& request);
  void RefreshDisplayState();

  std::shared_ptr<GpuContext> context_;
  GpuPresenterOptions options_;
  ShaderPtr vertex_shader_;
  ShaderPtr fragment_shader_;
  SamplerPtr luma_sampler_;
  SamplerPtr chroma_sampler_;
  std::map<SDL_GPUTextureFormat, PipelinePtr> pipelines_;
  PlaneTextures planes_;
  std::optional<BoundFrame> current_;
  TexturePtr offscreen_target_;
  PixelSize target_pixels_;
  float pixel_density_ = 1.0F;
  RefreshRate refresh_rate_;
  PresenterStats stats_;
  std::shared_ptr<PresenterObserver> observer_;
  uint64_t refresh_sequence_ = 0;
};

}

#endif
