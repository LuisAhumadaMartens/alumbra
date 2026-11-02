#include "penumbra/desktop/video/gpu_presenter.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/plane_layout.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/shader_blobs.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/rational.h"

namespace penumbra::desktop {
namespace {

constexpr SDL_GPUTextureFormat kOffscreenFormat =
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr size_t kRgbaBytesPerPixel = 4;
constexpr uint32_t kMinFramesInFlight = 1;
constexpr uint32_t kMaxFramesInFlight = 3;
constexpr Uint32 kFragmentTextureCount = 3;

absl::Status SdlError(absl::string_view what) {
  return absl::UnavailableError(absl::StrCat(what, ": ", SDL_GetError()));
}

SDL_GPUTextureFormat TextureFormatOf(PlaneTexelFormat format) {
  switch (format) {
    case PlaneTexelFormat::kR8:
      return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case PlaneTexelFormat::kR8G8:
      return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case PlaneTexelFormat::kR16:
      return SDL_GPU_TEXTUREFORMAT_R16_UNORM;
    case PlaneTexelFormat::kR16G16:
      return SDL_GPU_TEXTUREFORMAT_R16G16_UNORM;
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

RefreshRate ReadRefreshRate(SDL_Window* window) {
  RefreshRate rate;
  if (window == nullptr) {
    return rate;
  }
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  if (display == 0) {
    return rate;
  }
  const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
  if (mode == nullptr || mode->refresh_rate_numerator <= 0 ||
      mode->refresh_rate_denominator <= 0) {
    return rate;
  }
  const std::optional<Rational> hertz = Rational::Create(
      mode->refresh_rate_numerator, mode->refresh_rate_denominator);
  if (hertz.has_value()) {
    rate.hertz = *hertz;
  }
  return rate;
}

int64_t NowTicks() { return static_cast<int64_t>(SDL_GetTicksNS()); }

bool IsValidCpuFrame(const DecodedVideoFrame& frame,
                     const PlaneLayout& layout) {
  for (size_t index = 0; index < layout.plane_count; ++index) {
    const VideoPlane& plane = frame.planes.at(index);
    if (plane.data == nullptr ||
        static_cast<size_t>(std::max(plane.stride, 0)) <
            layout.planes.at(index).RowBytes()) {
      return false;
    }
  }
  return true;
}

void CopyPlane(const VideoPlane& source, const PlaneSpec& spec,
               std::span<uint8_t> destination) {
  const size_t row_bytes = spec.RowBytes();
  const auto stride = static_cast<size_t>(source.stride);
  const auto rows = static_cast<size_t>(spec.height);
  const std::span<const uint8_t> input(source.data,
                                       ((rows - 1) * stride) + row_bytes);
  for (size_t row = 0; row < rows; ++row) {
    const std::span<const uint8_t> line =
        input.subspan(row * stride, row_bytes);
    std::ranges::copy(line, destination.subspan(row * row_bytes).begin());
  }
}

SDL_GPUTextureRegion WholeTexture(SDL_GPUTexture* texture, PixelSize size) {
  return SDL_GPUTextureRegion{.texture = texture,
                              .mip_level = 0,
                              .layer = 0,
                              .x = 0,
                              .y = 0,
                              .z = 0,
                              .w = static_cast<Uint32>(size.width),
                              .h = static_cast<Uint32>(size.height),
                              .d = 1};
}

SDL_GPUTextureTransferInfo WholeTransfer(SDL_GPUTransferBuffer* buffer,
                                         PixelSize size) {
  return SDL_GPUTextureTransferInfo{
      .transfer_buffer = buffer,
      .offset = 0,
      .pixels_per_row = static_cast<Uint32>(size.width),
      .rows_per_layer = static_cast<Uint32>(size.height)};
}

}

GpuContext::~GpuContext() {
  if (device == nullptr) {
    return;
  }
  SDL_WaitForGPUIdle(device);
  if (claimed_window != nullptr) {
    SDL_ReleaseWindowFromGPUDevice(device, claimed_window);
  }
  SDL_DestroyGPUDevice(device);
}

TexturePlanesSurface::~TexturePlanesSurface() {
  for (SDL_GPUTexture* texture : textures_) {
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(context_->device, texture);
      context_->counters.textures.fetch_sub(1);
    }
  }
}

void GpuPresenter::TextureDeleter::operator()(SDL_GPUTexture* texture) const {
  SDL_ReleaseGPUTexture(context->device, texture);
  context->counters.textures.fetch_sub(1);
}

void GpuPresenter::TransferBufferDeleter::operator()(
    SDL_GPUTransferBuffer* buffer) const {
  SDL_ReleaseGPUTransferBuffer(context->device, buffer);
  context->counters.transfer_buffers.fetch_sub(1);
}

void GpuPresenter::ShaderDeleter::operator()(SDL_GPUShader* shader) const {
  SDL_ReleaseGPUShader(context->device, shader);
  context->counters.shaders.fetch_sub(1);
}

void GpuPresenter::PipelineDeleter::operator()(
    SDL_GPUGraphicsPipeline* pipeline) const {
  SDL_ReleaseGPUGraphicsPipeline(context->device, pipeline);
  context->counters.pipelines.fetch_sub(1);
}

void GpuPresenter::SamplerDeleter::operator()(SDL_GPUSampler* sampler) const {
  SDL_ReleaseGPUSampler(context->device, sampler);
  context->counters.samplers.fetch_sub(1);
}

absl::StatusOr<std::unique_ptr<GpuPresenter>> GpuPresenter::Create(
    const GpuPresenterOptions& options) {
  if (options.frames_in_flight < kMinFramesInFlight ||
      options.frames_in_flight > kMaxFramesInFlight) {
    return absl::InvalidArgumentError("frames_in_flight must be 1 to 3");
  }
  auto context = std::make_shared<GpuContext>();
  context->device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                          options.debug_mode, nullptr);
  if (context->device == nullptr) {
    return SdlError("SDL_CreateGPUDevice");
  }
  std::unique_ptr<GpuPresenter> presenter(
      new GpuPresenter(std::move(context), options));
  if (absl::Status status = presenter->Initialize(); !status.ok()) {
    return status;
  }
  return presenter;
}

GpuPresenter::GpuPresenter(std::shared_ptr<GpuContext> context,
                           const GpuPresenterOptions& options)
    : context_(std::move(context)), options_(options) {}

GpuPresenter::~GpuPresenter() {
  if (context_ != nullptr && context_->device != nullptr) {
    SDL_WaitForGPUIdle(context_->device);
  }
  current_.reset();
  planes_ = PlaneTextures{};
  offscreen_target_.reset();
  pipelines_.clear();
  vertex_shader_.reset();
  fragment_shader_.reset();
  luma_sampler_.reset();
  chroma_sampler_.reset();
}

absl::Status GpuPresenter::Initialize() {
  if (!SDL_SetGPUAllowedFramesInFlight(context_->device,
                                       options_.frames_in_flight)) {
    return SdlError("SDL_SetGPUAllowedFramesInFlight");
  }
  if (absl::Status status = CreateShaders(); !status.ok()) {
    return status;
  }
  if (absl::Status status = CreateSamplers(); !status.ok()) {
    return status;
  }
  if (options_.window != nullptr) {
    if (!SDL_WindowSupportsGPUPresentMode(context_->device, options_.window,
                                          SDL_GPU_PRESENTMODE_VSYNC)) {
      return SdlError("SDL_WindowSupportsGPUPresentMode");
    }
    if (!SDL_ClaimWindowForGPUDevice(context_->device, options_.window)) {
      return SdlError("SDL_ClaimWindowForGPUDevice");
    }
    context_->claimed_window = options_.window;
    if (!SDL_SetGPUSwapchainParameters(context_->device, options_.window,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
      return SdlError("SDL_SetGPUSwapchainParameters");
    }
  } else if (absl::Status status =
                 ResizeOffscreen(options_.offscreen_logical_size,
                                 options_.offscreen_pixel_density);
             !status.ok()) {
    return status;
  }
  RefreshDisplayState();
  return absl::OkStatus();
}

absl::Status GpuPresenter::CreateShaders() {
  const SDL_GPUShaderFormat supported =
      SDL_GetGPUShaderFormats(context_->device);
  const auto make = [this, supported](
                        const ShaderBinaries& binaries,
                        SDL_GPUShaderStage stage, Uint32 samplers,
                        Uint32 uniform_buffers) -> absl::StatusOr<ShaderPtr> {
    SDL_GPUShaderCreateInfo info{};
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0 && !binaries.msl.empty()) {
      info.code = binaries.msl.data();
      info.code_size = binaries.msl.size();
      info.entrypoint = kMslEntryPoint.data();
      info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0 &&
               !binaries.spirv.empty()) {
      info.code = binaries.spirv.data();
      info.code_size = binaries.spirv.size();
      info.entrypoint = kSpirvEntryPoint.data();
      info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else {
      return absl::UnavailableError(
          "The GPU device accepts neither SPIR-V nor MSL shaders");
    }
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniform_buffers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(context_->device, &info);
    if (shader == nullptr) {
      return SdlError("SDL_CreateGPUShader");
    }
    context_->counters.shaders.fetch_add(1);
    return ShaderPtr(shader, ShaderDeleter{context_});
  };
  absl::StatusOr<ShaderPtr> vertex =
      make(FullscreenTriangleVertexShader(), SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  if (!vertex.ok()) {
    return vertex.status();
  }
  absl::StatusOr<ShaderPtr> fragment =
      make(YuvToRgbFragmentShader(), SDL_GPU_SHADERSTAGE_FRAGMENT,
           kFragmentTextureCount, 1);
  if (!fragment.ok()) {
    return fragment.status();
  }
  vertex_shader_ = *std::move(vertex);
  fragment_shader_ = *std::move(fragment);
  return absl::OkStatus();
}

absl::Status GpuPresenter::CreateSamplers() {
  const auto make = [this](SDL_GPUFilter filter) -> absl::StatusOr<SamplerPtr> {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = filter;
    info.mag_filter = filter;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(context_->device, &info);
    if (sampler == nullptr) {
      return SdlError("SDL_CreateGPUSampler");
    }
    context_->counters.samplers.fetch_add(1);
    return SamplerPtr(sampler, SamplerDeleter{context_});
  };
  const SDL_GPUFilter luma_filter =
      options_.scaling_filter == ScalingFilter::kLinear
          ? SDL_GPU_FILTER_LINEAR
          : SDL_GPU_FILTER_NEAREST;
  absl::StatusOr<SamplerPtr> luma = make(luma_filter);
  if (!luma.ok()) {
    return luma.status();
  }
  absl::StatusOr<SamplerPtr> chroma = make(SDL_GPU_FILTER_LINEAR);
  if (!chroma.ok()) {
    return chroma.status();
  }
  SDL_WaitForGPUIdle(context_->device);
  luma_sampler_ = *std::move(luma);
  chroma_sampler_ = *std::move(chroma);
  return absl::OkStatus();
}

absl::Status GpuPresenter::SetScalingFilter(ScalingFilter filter) {
  if (options_.scaling_filter == filter) {
    return absl::OkStatus();
  }
  options_.scaling_filter = filter;
  return CreateSamplers();
}

void GpuPresenter::SetObserver(std::shared_ptr<PresenterObserver> observer) {
  observer_ = std::move(observer);
}

RefreshRate GpuPresenter::CurrentRefreshRate() const { return refresh_rate_; }

PresenterStats GpuPresenter::Stats() const { return stats_; }

PresenterCapabilities GpuPresenter::Capabilities() const {
  PresenterCapabilities capabilities;
  capabilities.backend = DriverName();
  capabilities.offscreen = options_.window == nullptr;
  capabilities.imported_surface_kinds = {SurfaceKind::kTexturePlanes};
  capabilities.present_timing = PresentTimingSupport::kNone;
  capabilities.present_timing_fallback_reason =
      "No platform presentation-time source is implemented yet; present "
      "records carry swapchain acquire and submit times only";
  capabilities.cpu_formats = {PixelFormat::kI420, PixelFormat::kNv12,
                              PixelFormat::kP010};
  return capabilities;
}

std::string GpuPresenter::DriverName() const {
  const char* driver = SDL_GetGPUDeviceDriver(context_->device);
  return driver == nullptr ? std::string() : std::string(driver);
}

void GpuPresenter::RefreshDisplayState() {
  if (options_.window == nullptr) {
    return;
  }
  const float density = SDL_GetWindowPixelDensity(options_.window);
  if (density > 0.0F) {
    pixel_density_ = density;
  }
  const RefreshRate rate = ReadRefreshRate(options_.window);
  if (rate.hertz != refresh_rate_.hertz) {
    refresh_rate_ = rate;
    if (observer_ != nullptr) {
      observer_->OnRefreshRateChanged(refresh_rate_);
    }
  }
}

bool GpuPresenter::HandleEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
      RefreshDisplayState();
      return true;
    default:
      return false;
  }
}

absl::StatusOr<GpuPresenter::TexturePtr> GpuPresenter::CreatePlaneTexture(
    const PlaneSpec& spec) {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = TextureFormatOf(spec.format);
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = static_cast<Uint32>(spec.width);
  info.height = static_cast<Uint32>(spec.height);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(context_->device, &info);
  if (texture == nullptr) {
    return SdlError("SDL_CreateGPUTexture (plane)");
  }
  context_->counters.textures.fetch_add(1);
  return TexturePtr(texture, TextureDeleter{context_});
}

absl::StatusOr<GpuPresenter::TransferBufferPtr>
GpuPresenter::CreateTransferBuffer(SDL_GPUTransferBufferUsage usage,
                                   size_t size) {
  SDL_GPUTransferBufferCreateInfo info{};
  info.usage = usage;
  info.size = static_cast<Uint32>(size);
  SDL_GPUTransferBuffer* buffer =
      SDL_CreateGPUTransferBuffer(context_->device, &info);
  if (buffer == nullptr) {
    return SdlError("SDL_CreateGPUTransferBuffer");
  }
  context_->counters.transfer_buffers.fetch_add(1);
  return TransferBufferPtr(buffer, TransferBufferDeleter{context_});
}

absl::StatusOr<GpuPresenter::TexturePtr> GpuPresenter::CreateRenderTarget(
    PixelSize size) {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = kOffscreenFormat;
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = static_cast<Uint32>(size.width);
  info.height = static_cast<Uint32>(size.height);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(context_->device, &info);
  if (texture == nullptr) {
    return SdlError("SDL_CreateGPUTexture (target)");
  }
  context_->counters.textures.fetch_add(1);
  return TexturePtr(texture, TextureDeleter{context_});
}

absl::Status GpuPresenter::ResizeOffscreen(PixelSize logical_size,
                                           float pixel_density) {
  if (options_.window != nullptr) {
    return absl::FailedPreconditionError(
        "ResizeOffscreen needs an offscreen presenter");
  }
  const PixelSize pixels = ScaleToPixels(logical_size, pixel_density);
  if (pixels.width <= 0 || pixels.height <= 0) {
    return absl::InvalidArgumentError("Offscreen size must be positive");
  }
  absl::StatusOr<TexturePtr> target = CreateRenderTarget(pixels);
  if (!target.ok()) {
    return target.status();
  }
  SDL_WaitForGPUIdle(context_->device);
  offscreen_target_ = *std::move(target);
  target_pixels_ = pixels;
  pixel_density_ = pixel_density;
  return absl::OkStatus();
}

absl::StatusOr<SDL_GPUGraphicsPipeline*> GpuPresenter::PipelineFor(
    SDL_GPUTextureFormat format) {
  const auto found = pipelines_.find(format);
  if (found != pipelines_.end()) {
    return found->second.get();
  }
  SDL_GPUColorTargetDescription target{};
  target.format = format;
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertex_shader_.get();
  info.fragment_shader = fragment_shader_.get();
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  SDL_GPUGraphicsPipeline* pipeline =
      SDL_CreateGPUGraphicsPipeline(context_->device, &info);
  if (pipeline == nullptr) {
    return SdlError("SDL_CreateGPUGraphicsPipeline");
  }
  context_->counters.pipelines.fetch_add(1);
  const auto inserted = pipelines_.emplace(
      format, PipelinePtr(pipeline, PipelineDeleter{context_}));
  return inserted.first->second.get();
}

absl::Status GpuPresenter::PrepareCpuFrame(const DecodedVideoFrame& frame) {
  const std::optional<PlaneLayout> layout =
      DescribePlanes(frame.format, frame.width, frame.height);
  if (!layout.has_value()) {
    return absl::InvalidArgumentError("Unsupported frame format or size");
  }
  if (!IsValidCpuFrame(frame, *layout)) {
    return absl::InvalidArgumentError("Frame plane is missing or too narrow");
  }
  const PixelSize size{.width = frame.width, .height = frame.height};
  if (planes_.format == frame.format && planes_.size == size) {
    return absl::OkStatus();
  }
  PlaneTextures fresh;
  fresh.format = frame.format;
  fresh.size = size;
  for (size_t index = 0; index < layout->plane_count; ++index) {
    PlaneResource resource;
    resource.spec = layout->planes.at(index);
    absl::StatusOr<TexturePtr> texture = CreatePlaneTexture(resource.spec);
    if (!texture.ok()) {
      return texture.status();
    }
    resource.texture = *std::move(texture);
    absl::StatusOr<TransferBufferPtr> upload = CreateTransferBuffer(
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, resource.spec.ByteSize());
    if (!upload.ok()) {
      return upload.status();
    }
    resource.upload = *std::move(upload);
    fresh.planes.push_back(std::move(resource));
  }
  SDL_WaitForGPUIdle(context_->device);
  planes_ = std::move(fresh);
  return absl::OkStatus();
}

absl::Status GpuPresenter::UploadPlanes(SDL_GPUCommandBuffer* commands,
                                        const DecodedVideoFrame& frame) {
  size_t index = 0;
  for (const PlaneResource& plane : planes_.planes) {
    void* mapped =
        SDL_MapGPUTransferBuffer(context_->device, plane.upload.get(), true);
    if (mapped == nullptr) {
      return SdlError("SDL_MapGPUTransferBuffer");
    }
    CopyPlane(frame.planes.at(index), plane.spec,
              std::span<uint8_t>(static_cast<uint8_t*>(mapped),
                                 plane.spec.ByteSize()));
    SDL_UnmapGPUTransferBuffer(context_->device, plane.upload.get());
    ++index;
  }
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
  if (copy == nullptr) {
    return SdlError("SDL_BeginGPUCopyPass");
  }
  for (const PlaneResource& plane : planes_.planes) {
    const PixelSize size{.width = plane.spec.width,
                         .height = plane.spec.height};
    const SDL_GPUTextureTransferInfo source =
        WholeTransfer(plane.upload.get(), size);
    const SDL_GPUTextureRegion destination =
        WholeTexture(plane.texture.get(), size);
    SDL_UploadToGPUTexture(copy, &source, &destination, true);
  }
  SDL_EndGPUCopyPass(copy);
  return absl::OkStatus();
}

absl::Status GpuPresenter::BindOpaqueFrame(const DecodedVideoFrame& frame) {
  if (frame.storage == nullptr) {
    return absl::InvalidArgumentError("Opaque frame has no surface storage");
  }
  const auto* handle =
      static_cast<const GpuSurfaceHandle*>(frame.storage.get());
  const auto* surface = dynamic_cast<const TexturePlanesSurface*>(handle);
  if (surface == nullptr) {
    return absl::UnimplementedError("Surface kind has no import path");
  }
  if (surface->Context() != context_.get()) {
    return absl::InvalidArgumentError("Surface belongs to another GPU device");
  }
  BoundFrame bound;
  bound.format = surface->Format();
  bound.size = surface->Size();
  bound.color = frame.color;
  bound.textures = surface->Textures();
  bound.keep_alive = frame.storage;
  current_ = std::move(bound);
  return absl::OkStatus();
}

absl::Status GpuPresenter::BindNewFrame(const DecodedVideoFrame& frame) {
  if (frame.format == PixelFormat::kOpaque) {
    return BindOpaqueFrame(frame);
  }
  return PrepareCpuFrame(frame);
}

absl::StatusOr<GpuPresenter::TargetSurface> GpuPresenter::AcquireTarget(
    SDL_GPUCommandBuffer* commands) {
  TargetSurface target;
  if (options_.window == nullptr) {
    target.texture = offscreen_target_.get();
    target.format = kOffscreenFormat;
    target.size = target_pixels_;
    return target;
  }
  Uint32 width = 0;
  Uint32 height = 0;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(
          commands, options_.window, &target.texture, &width, &height)) {
    return SdlError("SDL_WaitAndAcquireGPUSwapchainTexture");
  }
  target.format =
      SDL_GetGPUSwapchainTextureFormat(context_->device, options_.window);
  target.size = PixelSize{.width = static_cast<int32_t>(width),
                          .height = static_cast<int32_t>(height)};
  return target;
}

void GpuPresenter::DrawVideo(SDL_GPURenderPass* pass,
                             SDL_GPUCommandBuffer* commands,
                             const BoundFrame& frame,
                             const ConversionParams& params,
                             SDL_GPUGraphicsPipeline* pipeline,
                             const PixelRect& video_rect) {
  SDL_BindGPUGraphicsPipeline(pass, pipeline);
  const SDL_GPUViewport viewport{.x = static_cast<float>(video_rect.x),
                                 .y = static_cast<float>(video_rect.y),
                                 .w = static_cast<float>(video_rect.width),
                                 .h = static_cast<float>(video_rect.height),
                                 .min_depth = 0.0F,
                                 .max_depth = 1.0F};
  SDL_SetGPUViewport(pass, &viewport);
  const SDL_Rect scissor{.x = video_rect.x,
                         .y = video_rect.y,
                         .w = video_rect.width,
                         .h = video_rect.height};
  SDL_SetGPUScissor(pass, &scissor);
  const bool semi_planar = frame.format != PixelFormat::kI420;
  const std::array<SDL_GPUTextureSamplerBinding, kFragmentTextureCount>
      bindings = {
          SDL_GPUTextureSamplerBinding{.texture = frame.textures.at(0),
                                       .sampler = luma_sampler_.get()},
          SDL_GPUTextureSamplerBinding{.texture = frame.textures.at(1),
                                       .sampler = chroma_sampler_.get()},
          SDL_GPUTextureSamplerBinding{
              .texture = frame.textures.at(semi_planar ? 1 : 2),
              .sampler = chroma_sampler_.get()}};
  SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(),
                              static_cast<Uint32>(bindings.size()));
  SDL_PushGPUFragmentUniformData(commands, 0, &params, sizeof(params));
  SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

absl::StatusOr<PixelRect> GpuPresenter::RecordVideoPass(
    SDL_GPUCommandBuffer* commands, const TargetSurface& target,
    const PresentRequest& request) {
  SDL_GPUColorTargetInfo color_target{};
  color_target.texture = target.texture;
  color_target.clear_color =
      SDL_FColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
  color_target.load_op = SDL_GPU_LOADOP_CLEAR;
  color_target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* pass =
      SDL_BeginGPURenderPass(commands, &color_target, 1, nullptr);
  if (pass == nullptr) {
    return SdlError("SDL_BeginGPURenderPass");
  }
  PixelRect video_rect;
  if (current_.has_value()) {
    const BoundFrame frame = *current_;
    const PixelRect bounds =
        request.bounds.value_or(PixelRect{.x = 0,
                                          .y = 0,
                                          .width = target.size.width,
                                          .height = target.size.height});
    const DisplayAspect fallback{.numerator = frame.size.width,
                                 .denominator = frame.size.height};
    video_rect = FitInside(
        bounds, ComputeDisplayAspect(frame.size, request.sample_aspect_ratio)
                    .value_or(fallback));
    const std::optional<ConversionParams> params = ComputeConversionParams(
        frame.format, frame.color, frame.size.width, frame.size.height);
    const absl::StatusOr<SDL_GPUGraphicsPipeline*> pipeline =
        PipelineFor(target.format);
    if (params.has_value() && pipeline.ok() && video_rect.width > 0 &&
        video_rect.height > 0) {
      DrawVideo(pass, commands, frame, *params, *pipeline, video_rect);
    }
  }
  SDL_EndGPURenderPass(pass);
  return video_rect;
}

void GpuPresenter::CountDrop(const PresentRequest& request) {
  if (request.frame != nullptr) {
    ++stats_.dropped_frames;
  }
}

void GpuPresenter::RecordPresented(const PresentRequest& request,
                                   int64_t acquire_ticks, int64_t submit_ticks,
                                   const PixelRect& video_rect) {
  const bool new_frame = request.frame != nullptr;
  ++refresh_sequence_;
  ++stats_.submitted_refreshes;
  stats_.last_acquire_ticks_ns = acquire_ticks;
  stats_.last_submit_ticks_ns = submit_ticks;
  if (new_frame) {
    ++stats_.presented_frames;
    stats_.last_presented_media_time = request.frame->presentation_time;
  } else {
    ++stats_.repeated_refreshes;
  }
  if (observer_ == nullptr) {
    return;
  }
  PresentRecord record;
  record.refresh_sequence = refresh_sequence_;
  record.new_frame = new_frame;
  record.media_time =
      new_frame ? request.frame->presentation_time : MediaTime::Unknown();
  record.acquire_ticks_ns = acquire_ticks;
  record.submit_ticks_ns = submit_ticks;
  record.target_pixels = target_pixels_;
  record.video_rect = video_rect;
  observer_->OnPresented(record);
}

absl::Status GpuPresenter::Present(const PresentRequest& request) {
  if (request.frame != nullptr) {
    if (absl::Status status = BindNewFrame(*request.frame); !status.ok()) {
      CountDrop(request);
      return status;
    }
  }
  SDL_GPUCommandBuffer* commands =
      SDL_AcquireGPUCommandBuffer(context_->device);
  if (commands == nullptr) {
    CountDrop(request);
    return SdlError("SDL_AcquireGPUCommandBuffer");
  }
  const bool upload =
      request.frame != nullptr && request.frame->format != PixelFormat::kOpaque;
  if (upload) {
    if (absl::Status status = UploadPlanes(commands, *request.frame);
        !status.ok()) {
      SDL_CancelGPUCommandBuffer(commands);
      CountDrop(request);
      return status;
    }
    BoundFrame bound;
    bound.format = request.frame->format;
    bound.size = planes_.size;
    bound.color = request.frame->color;
    size_t index = 0;
    for (const PlaneResource& plane : planes_.planes) {
      bound.textures.at(index) = plane.texture.get();
      ++index;
    }
    current_ = std::move(bound);
  }
  const absl::StatusOr<TargetSurface> target = AcquireTarget(commands);
  if (!target.ok()) {
    SDL_CancelGPUCommandBuffer(commands);
    CountDrop(request);
    return target.status();
  }
  const int64_t acquire_ticks = NowTicks();
  if (target->texture == nullptr) {
    SDL_SubmitGPUCommandBuffer(commands);
    CountDrop(request);
    return absl::OkStatus();
  }
  target_pixels_ = target->size;
  const absl::StatusOr<PixelRect> video_rect =
      RecordVideoPass(commands, *target, request);
  if (!video_rect.ok()) {
    SDL_SubmitGPUCommandBuffer(commands);
    CountDrop(request);
    return video_rect.status();
  }
  if (!SDL_SubmitGPUCommandBuffer(commands)) {
    CountDrop(request);
    return SdlError("SDL_SubmitGPUCommandBuffer");
  }
  RecordPresented(request, acquire_ticks, NowTicks(), *video_rect);
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> GpuPresenter::ReadbackTarget() {
  if (options_.window != nullptr || offscreen_target_ == nullptr) {
    return absl::FailedPreconditionError("Readback needs an offscreen target");
  }
  const size_t byte_count = static_cast<size_t>(target_pixels_.width) *
                            static_cast<size_t>(target_pixels_.height) *
                            kRgbaBytesPerPixel;
  const absl::StatusOr<TransferBufferPtr> buffer =
      CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, byte_count);
  if (!buffer.ok()) {
    return buffer.status();
  }
  SDL_GPUCommandBuffer* commands =
      SDL_AcquireGPUCommandBuffer(context_->device);
  if (commands == nullptr) {
    return SdlError("SDL_AcquireGPUCommandBuffer (readback)");
  }
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
  if (copy == nullptr) {
    SDL_CancelGPUCommandBuffer(commands);
    return SdlError("SDL_BeginGPUCopyPass (readback)");
  }
  const SDL_GPUTextureRegion source =
      WholeTexture(offscreen_target_.get(), target_pixels_);
  const SDL_GPUTextureTransferInfo destination =
      WholeTransfer(buffer->get(), target_pixels_);
  SDL_DownloadFromGPUTexture(copy, &source, &destination);
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
  if (fence == nullptr) {
    return SdlError("SDL_SubmitGPUCommandBufferAndAcquireFence");
  }
  context_->counters.fences.fetch_add(1);
  const bool waited = SDL_WaitForGPUFences(context_->device, true, &fence, 1);
  SDL_ReleaseGPUFence(context_->device, fence);
  context_->counters.fences.fetch_sub(1);
  if (!waited) {
    return SdlError("SDL_WaitForGPUFences");
  }
  const void* const mapped =
      SDL_MapGPUTransferBuffer(context_->device, buffer->get(), false);
  if (mapped == nullptr) {
    return SdlError("SDL_MapGPUTransferBuffer (download)");
  }
  const std::span<const uint8_t> pixels(static_cast<const uint8_t*>(mapped),
                                        byte_count);
  RgbaImage image;
  image.size = target_pixels_;
  image.pixels.assign(pixels.begin(), pixels.end());
  SDL_UnmapGPUTransferBuffer(context_->device, buffer->get());
  return image;
}

absl::StatusOr<std::shared_ptr<const GpuSurfaceHandle>>
GpuPresenter::CreateTestSurface(const DecodedVideoFrame& frame) {
  const std::optional<PlaneLayout> layout =
      DescribePlanes(frame.format, frame.width, frame.height);
  if (!layout.has_value()) {
    return absl::InvalidArgumentError("Unsupported frame format or size");
  }
  if (!IsValidCpuFrame(frame, *layout)) {
    return absl::InvalidArgumentError("Frame plane is missing or too narrow");
  }
  SDL_GPUCommandBuffer* commands =
      SDL_AcquireGPUCommandBuffer(context_->device);
  if (commands == nullptr) {
    return SdlError("SDL_AcquireGPUCommandBuffer (surface)");
  }
  std::vector<PlaneResource> planes;
  for (size_t index = 0; index < layout->plane_count; ++index) {
    PlaneResource resource;
    resource.spec = layout->planes.at(index);
    absl::StatusOr<TexturePtr> texture = CreatePlaneTexture(resource.spec);
    absl::StatusOr<TransferBufferPtr> upload = CreateTransferBuffer(
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, resource.spec.ByteSize());
    if (!texture.ok() || !upload.ok()) {
      SDL_CancelGPUCommandBuffer(commands);
      return texture.ok() ? upload.status() : texture.status();
    }
    resource.texture = *std::move(texture);
    resource.upload = *std::move(upload);
    void* mapped = SDL_MapGPUTransferBuffer(context_->device,
                                            resource.upload.get(), false);
    if (mapped == nullptr) {
      SDL_CancelGPUCommandBuffer(commands);
      return SdlError("SDL_MapGPUTransferBuffer (surface)");
    }
    CopyPlane(frame.planes.at(index), resource.spec,
              std::span<uint8_t>(static_cast<uint8_t*>(mapped),
                                 resource.spec.ByteSize()));
    SDL_UnmapGPUTransferBuffer(context_->device, resource.upload.get());
    planes.push_back(std::move(resource));
  }
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
  if (copy == nullptr) {
    SDL_CancelGPUCommandBuffer(commands);
    return SdlError("SDL_BeginGPUCopyPass (surface)");
  }
  for (const PlaneResource& plane : planes) {
    const PixelSize size{.width = plane.spec.width,
                         .height = plane.spec.height};
    const SDL_GPUTextureTransferInfo source =
        WholeTransfer(plane.upload.get(), size);
    const SDL_GPUTextureRegion destination =
        WholeTexture(plane.texture.get(), size);
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
  }
  SDL_EndGPUCopyPass(copy);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
  if (fence == nullptr) {
    return SdlError("SDL_SubmitGPUCommandBufferAndAcquireFence (surface)");
  }
  const bool waited = SDL_WaitForGPUFences(context_->device, true, &fence, 1);
  SDL_ReleaseGPUFence(context_->device, fence);
  if (!waited) {
    return SdlError("SDL_WaitForGPUFences (surface)");
  }
  SurfaceTextures textures{};
  size_t index = 0;
  for (PlaneResource& plane : planes) {
    textures.at(index) = plane.texture.release();
    ++index;
  }
  return std::make_shared<TexturePlanesSurface>(
      context_, frame.format,
      PixelSize{.width = frame.width, .height = frame.height}, textures);
}

}
