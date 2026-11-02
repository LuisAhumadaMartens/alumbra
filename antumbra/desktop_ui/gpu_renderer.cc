#include "alumbra/desktop_ui/gpu_renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/glyph_atlas.h"
#include "alumbra/desktop_ui/icons.h"
#include "alumbra/desktop_ui/structure.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/overlay.h"
#include "penumbra/desktop/video/shader_blobs.h"

namespace alumbra::desktop_ui {
namespace {

using penumbra::desktop::GpuContext;
using penumbra::desktop::GpuResourceCounters;
using penumbra::desktop::OverlayTarget;

constexpr std::size_t kVerticesPerQuad = 6;
constexpr float kFringe = 1.0F;
constexpr float kKindBox = 0.0F;
constexpr float kKindGlyph = 1.0F;
constexpr float kKindIcon = 2.0F;
constexpr std::size_t kMinimumVertexBytes = 65536;
constexpr SDL_GPUTextureFormat kAtlasFormat = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
constexpr Uint32 kFragmentSamplerCount = 2;

absl::Status SdlError(absl::string_view what) {
  return absl::UnavailableError(absl::StrCat(what, ": ", SDL_GetError()));
}

SDL_Rect DeviceClip(const Rect& rect, float density) {
  const int left = static_cast<int>(std::floor(rect.x * density));
  const int top = static_cast<int>(std::floor(rect.y * density));
  const int right = static_cast<int>(std::ceil(rect.Right() * density));
  const int bottom = static_cast<int>(std::ceil(rect.Bottom() * density));
  return SDL_Rect{left, top, std::max(0, right - left),
                  std::max(0, bottom - top)};
}

SDL_Rect Intersect(const SDL_Rect& first, const SDL_Rect& second) {
  const int left = std::max(first.x, second.x);
  const int top = std::max(first.y, second.y);
  const int right = std::min(first.x + first.w, second.x + second.w);
  const int bottom = std::min(first.y + first.h, second.y + second.h);
  return SDL_Rect{left, top, std::max(0, right - left),
                  std::max(0, bottom - top)};
}

SDL_GPUTextureRegion WholeTexture(SDL_GPUTexture* texture, int size) {
  SDL_GPUTextureRegion region{};
  region.texture = texture;
  region.w = static_cast<Uint32>(size);
  region.h = static_cast<Uint32>(size);
  region.d = 1;
  return region;
}

SDL_GPUTextureTransferInfo WholeTransfer(SDL_GPUTransferBuffer* buffer,
                                         int size) {
  SDL_GPUTextureTransferInfo info{};
  info.transfer_buffer = buffer;
  info.pixels_per_row = static_cast<Uint32>(size);
  info.rows_per_layer = static_cast<Uint32>(size);
  return info;
}

}

absl::StatusOr<std::shared_ptr<UiGpuRenderer>> UiGpuRenderer::Create(
    std::shared_ptr<GpuContext> context, std::shared_ptr<const FontFace> font) {
  if (context == nullptr || context->device == nullptr || font == nullptr) {
    return absl::InvalidArgumentError("A GPU context and a font are required");
  }
  std::shared_ptr<UiGpuRenderer> renderer(
      new UiGpuRenderer(std::move(context), std::move(font)));
  if (absl::Status status = renderer->Initialize(); !status.ok()) {
    return status;
  }
  return renderer;
}

UiGpuRenderer::UiGpuRenderer(std::shared_ptr<GpuContext> context,
                             std::shared_ptr<const FontFace> font)
    : context_(std::move(context)),
      font_(std::move(font)),
      atlas_(kGlyphAtlasSize) {}

UiGpuRenderer::~UiGpuRenderer() {
  if (context_ != nullptr && context_->device != nullptr) {
    SDL_WaitForGPUIdle(context_->device);
  }
  current_pipeline_ = nullptr;
  pipelines_.clear();
}

absl::Status UiGpuRenderer::CreateShaders() {
  const SDL_GPUShaderFormat supported =
      SDL_GetGPUShaderFormats(context_->device);
  const auto make = [this, supported](
                        const penumbra::desktop::ShaderBinaries& binaries,
                        SDL_GPUShaderStage stage, Uint32 samplers,
                        Uint32 uniform_buffers) -> absl::StatusOr<ShaderPtr> {
    SDL_GPUShaderCreateInfo info{};
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0 && !binaries.msl.empty()) {
      info.code = binaries.msl.data();
      info.code_size = binaries.msl.size();
      info.entrypoint = penumbra::desktop::kMslEntryPoint.data();
      info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0 &&
               !binaries.spirv.empty()) {
      info.code = binaries.spirv.data();
      info.code_size = binaries.spirv.size();
      info.entrypoint = penumbra::desktop::kSpirvEntryPoint.data();
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
      return SdlError("SDL_CreateGPUShader (ui)");
    }
    context_->counters.shaders.fetch_add(1);
    return ShaderPtr(shader, GpuDeleter<SDL_GPUShader, SDL_ReleaseGPUShader>{
                                 .context = context_,
                                 .counter = &GpuResourceCounters::shaders});
  };
  absl::StatusOr<ShaderPtr> vertex = make(penumbra::desktop::UiVertexShader(),
                                          SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
  if (!vertex.ok()) {
    return vertex.status();
  }
  absl::StatusOr<ShaderPtr> fragment =
      make(penumbra::desktop::UiFragmentShader(), SDL_GPU_SHADERSTAGE_FRAGMENT,
           kFragmentSamplerCount, 0);
  if (!fragment.ok()) {
    return fragment.status();
  }
  vertex_shader_ = *std::move(vertex);
  fragment_shader_ = *std::move(fragment);
  return absl::OkStatus();
}

absl::Status UiGpuRenderer::CreateSamplers() {
  SDL_GPUSamplerCreateInfo info{};
  info.min_filter = SDL_GPU_FILTER_LINEAR;
  info.mag_filter = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  SDL_GPUSampler* sampler = SDL_CreateGPUSampler(context_->device, &info);
  if (sampler == nullptr) {
    return SdlError("SDL_CreateGPUSampler (ui)");
  }
  context_->counters.samplers.fetch_add(1);
  sampler_ = SamplerPtr(
      sampler,
      GpuDeleter<SDL_GPUSampler, SDL_ReleaseGPUSampler>{
          .context = context_, .counter = &GpuResourceCounters::samplers});
  return absl::OkStatus();
}

absl::StatusOr<UiGpuRenderer::TexturePtr> UiGpuRenderer::CreateAtlasTexture(
    int size) {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = kAtlasFormat;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = static_cast<Uint32>(size);
  info.height = static_cast<Uint32>(size);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(context_->device, &info);
  if (texture == nullptr) {
    return SdlError("SDL_CreateGPUTexture (ui atlas)");
  }
  context_->counters.textures.fetch_add(1);
  return TexturePtr(texture, GpuDeleter<SDL_GPUTexture, SDL_ReleaseGPUTexture>{
                                 .context = context_,
                                 .counter = &GpuResourceCounters::textures});
}

absl::StatusOr<UiGpuRenderer::TransferPtr> UiGpuRenderer::CreateTransfer(
    std::size_t size) {
  SDL_GPUTransferBufferCreateInfo info{};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = static_cast<Uint32>(size);
  SDL_GPUTransferBuffer* buffer =
      SDL_CreateGPUTransferBuffer(context_->device, &info);
  if (buffer == nullptr) {
    return SdlError("SDL_CreateGPUTransferBuffer (ui)");
  }
  context_->counters.transfer_buffers.fetch_add(1);
  return TransferPtr(
      buffer, GpuDeleter<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer>{
                  .context = context_,
                  .counter = &GpuResourceCounters::transfer_buffers});
}

absl::Status UiGpuRenderer::Initialize() {
  if (absl::Status status = CreateShaders(); !status.ok()) {
    return status;
  }
  if (absl::Status status = CreateSamplers(); !status.ok()) {
    return status;
  }
  absl::StatusOr<TexturePtr> glyph_texture =
      CreateAtlasTexture(kGlyphAtlasSize);
  if (!glyph_texture.ok()) {
    return glyph_texture.status();
  }
  glyph_texture_ = *std::move(glyph_texture);
  absl::StatusOr<TexturePtr> icon_texture = CreateAtlasTexture(kIconAtlasSize);
  if (!icon_texture.ok()) {
    return icon_texture.status();
  }
  icon_texture_ = *std::move(icon_texture);
  const auto glyph_bytes = static_cast<std::size_t>(kGlyphAtlasSize) *
                           static_cast<std::size_t>(kGlyphAtlasSize);
  absl::StatusOr<TransferPtr> atlas_transfer = CreateTransfer(glyph_bytes);
  if (!atlas_transfer.ok()) {
    return atlas_transfer.status();
  }
  atlas_transfer_ = *std::move(atlas_transfer);
  const auto icon_bytes = static_cast<std::size_t>(kIconAtlasSize) *
                          static_cast<std::size_t>(kIconAtlasSize);
  absl::StatusOr<TransferPtr> icon_transfer = CreateTransfer(icon_bytes);
  if (!icon_transfer.ok()) {
    return icon_transfer.status();
  }
  icon_transfer_ = *std::move(icon_transfer);
  void* mapped =
      SDL_MapGPUTransferBuffer(context_->device, icon_transfer_.get(), false);
  if (mapped == nullptr) {
    return SdlError("SDL_MapGPUTransferBuffer (icons)");
  }
  std::ranges::copy(
      icons_.Pixels(),
      std::span<std::uint8_t>(static_cast<std::uint8_t*>(mapped), icon_bytes)
          .begin());
  SDL_UnmapGPUTransferBuffer(context_->device, icon_transfer_.get());
  return absl::OkStatus();
}

absl::StatusOr<SDL_GPUGraphicsPipeline*> UiGpuRenderer::PipelineFor(
    SDL_GPUTextureFormat format) {
  const auto found = pipelines_.find(format);
  if (found != pipelines_.end()) {
    return found->second.get();
  }
  constexpr Uint32 kFloatSize = sizeof(float);
  SDL_GPUVertexBufferDescription buffer{};
  buffer.slot = 0;
  buffer.pitch = sizeof(Vertex);
  buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  std::array<SDL_GPUVertexAttribute, 5> attributes{};
  constexpr std::array<SDL_GPUVertexElementFormat, 5> kFormats = {
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3};
  constexpr std::array<Uint32, 5> kOffsets = {0, 4, 8, 12, 16};
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    attributes.at(index).location = static_cast<Uint32>(index);
    attributes.at(index).buffer_slot = 0;
    attributes.at(index).format = kFormats.at(index);
    attributes.at(index).offset = kOffsets.at(index) * kFloatSize;
  }
  SDL_GPUColorTargetDescription target{};
  target.format = format;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_color_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vertex_shader_.get();
  info.fragment_shader = fragment_shader_.get();
  info.vertex_input_state.vertex_buffer_descriptions = &buffer;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attributes.data();
  info.vertex_input_state.num_vertex_attributes =
      static_cast<Uint32>(attributes.size());
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  SDL_GPUGraphicsPipeline* pipeline =
      SDL_CreateGPUGraphicsPipeline(context_->device, &info);
  if (pipeline == nullptr) {
    return SdlError("SDL_CreateGPUGraphicsPipeline (ui)");
  }
  context_->counters.pipelines.fetch_add(1);
  const auto inserted = pipelines_.emplace(
      format,
      PipelinePtr(
          pipeline,
          GpuDeleter<SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline>{
              .context = context_,
              .counter = &GpuResourceCounters::pipelines}));
  return inserted.first->second.get();
}

absl::Status UiGpuRenderer::EnsureVertexCapacity(std::size_t bytes) {
  if (bytes <= vertex_capacity_) {
    return absl::OkStatus();
  }
  const std::size_t capacity =
      std::max({bytes, vertex_capacity_ * 2, kMinimumVertexBytes});
  SDL_GPUBufferCreateInfo info{};
  info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  info.size = static_cast<Uint32>(capacity);
  SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(context_->device, &info);
  if (buffer == nullptr) {
    return SdlError("SDL_CreateGPUBuffer (ui)");
  }
  context_->counters.buffers.fetch_add(1);
  BufferPtr owned(buffer, GpuDeleter<SDL_GPUBuffer, SDL_ReleaseGPUBuffer>{
                              .context = context_,
                              .counter = &GpuResourceCounters::buffers});
  absl::StatusOr<TransferPtr> transfer = CreateTransfer(capacity);
  if (!transfer.ok()) {
    return transfer.status();
  }
  SDL_WaitForGPUIdle(context_->device);
  vertex_buffer_ = std::move(owned);
  vertex_transfer_ = *std::move(transfer);
  vertex_capacity_ = capacity;
  return absl::OkStatus();
}

void UiGpuRenderer::AppendQuad(const Quad& quad, const Vertex& prototype) {
  struct Corner {
    float x;
    float y;
    float u;
    float v;
  };
  const std::array<Corner, 4> corners = {{
      {.x = quad.left, .y = quad.top, .u = quad.u0, .v = quad.v0},
      {.x = quad.right, .y = quad.top, .u = quad.u1, .v = quad.v0},
      {.x = quad.left, .y = quad.bottom, .u = quad.u0, .v = quad.v1},
      {.x = quad.right, .y = quad.bottom, .u = quad.u1, .v = quad.v1},
  }};
  constexpr std::array<std::size_t, kVerticesPerQuad> kOrder = {0, 1, 2,
                                                                1, 3, 2};
  for (const std::size_t index : kOrder) {
    const Corner& corner = corners.at(index);
    Vertex vertex = prototype;
    vertex.x = corner.x;
    vertex.y = corner.y;
    vertex.u = corner.u;
    vertex.v = corner.v;
    vertex.local_x = corner.x - quad.centre_x;
    vertex.local_y = corner.y - quad.centre_y;
    vertices_.push_back(vertex);
  }
}

void UiGpuRenderer::CloseBatch(const SDL_Rect& scissor) {
  const auto end = static_cast<std::uint32_t>(vertices_.size());
  if (end > batch_start_) {
    Batch batch;
    batch.scissor = scissor;
    batch.first = batch_start_;
    batch.count = end - batch_start_;
    batches_.push_back(batch);
    batch_start_ = end;
  }
}

void UiGpuRenderer::AppendBox(const RoundedRectCommand& box, float density) {
  const float width = box.rect.width * density;
  const float height = box.rect.height * density;
  if (width <= 0 || height <= 0) {
    return;
  }
  Vertex prototype;
  prototype.red = box.fill.red;
  prototype.green = box.fill.green;
  prototype.blue = box.fill.blue;
  prototype.alpha = box.fill.alpha;
  prototype.half_width = width / 2;
  prototype.half_height = height / 2;
  prototype.radius =
      std::min(box.radius * density, std::min(width, height) / 2);
  prototype.border_width = box.border_width * density;
  prototype.border_red = box.border.red;
  prototype.border_green = box.border.green;
  prototype.border_blue = box.border.blue;
  prototype.border_alpha = box.border.alpha;
  prototype.kind = kKindBox;
  Quad quad;
  quad.centre_x = (box.rect.x * density) + (width / 2);
  quad.centre_y = (box.rect.y * density) + (height / 2);
  quad.left = (box.rect.x * density) - kFringe;
  quad.top = (box.rect.y * density) - kFringe;
  quad.right = (box.rect.x * density) + width + kFringe;
  quad.bottom = (box.rect.y * density) + height + kFringe;
  AppendQuad(quad, prototype);
}

void UiGpuRenderer::AppendIcon(const IconCommand& icon, float density) {
  const float width = icon.rect.width * density;
  const float height = icon.rect.height * density;
  if (width <= 0 || height <= 0) {
    return;
  }
  const float scale = IconSdfAtlas::CellScale();
  const Point centre = icon.rect.Centre();
  const IconAtlasCell cell = IconSdfAtlas::Cell(icon.icon);
  Vertex prototype;
  prototype.red = icon.color.red;
  prototype.green = icon.color.green;
  prototype.blue = icon.color.blue;
  prototype.alpha = icon.color.alpha;
  prototype.kind = kKindIcon;
  Quad quad;
  quad.left = (centre.x * density) - (width * scale / 2);
  quad.right = (centre.x * density) + (width * scale / 2);
  quad.top = (centre.y * density) - (height * scale / 2);
  quad.bottom = (centre.y * density) + (height * scale / 2);
  quad.u0 = cell.u0;
  quad.v0 = cell.v0;
  quad.u1 = cell.u1;
  quad.v1 = cell.v1;
  AppendQuad(quad, prototype);
}

bool UiGpuRenderer::AppendGlyph(const GlyphCommand& glyph, float density,
                                bool allow_failure) {
  const std::optional<AtlasGlyph> found =
      atlas_.Lookup(*font_, glyph.glyph, glyph.font_size * density);
  if (!found.has_value()) {
    return allow_failure;
  }
  if (found->width <= 0 || found->height <= 0) {
    return true;
  }
  const float origin_x = std::round(glyph.origin.x * density);
  const float origin_y = std::round(glyph.origin.y * density);
  Vertex prototype;
  prototype.red = glyph.color.red;
  prototype.green = glyph.color.green;
  prototype.blue = glyph.color.blue;
  prototype.alpha = glyph.color.alpha;
  prototype.kind = kKindGlyph;
  Quad quad;
  quad.left = origin_x + static_cast<float>(found->left);
  quad.top = origin_y - static_cast<float>(found->top);
  quad.right = quad.left + static_cast<float>(found->width);
  quad.bottom = quad.top + static_cast<float>(found->height);
  quad.u0 = found->u0;
  quad.v0 = found->v0;
  quad.u1 = found->u1;
  quad.v1 = found->v1;
  AppendQuad(quad, prototype);
  return true;
}

bool UiGpuRenderer::Build(const OverlayTarget& target, bool allow_failure) {
  vertices_.clear();
  batches_.clear();
  batch_start_ = 0;
  const float density = target.pixel_density;
  const SDL_Rect full{0, 0, target.pixels.width, target.pixels.height};
  std::vector<SDL_Rect> clips;
  const auto current = [&]() -> SDL_Rect {
    return clips.empty() ? full : clips.back();
  };
  for (const DrawCommand& command : draw_list_.Commands()) {
    if (const auto* box = std::get_if<RoundedRectCommand>(&command)) {
      AppendBox(*box, density);
    } else if (const auto* icon = std::get_if<IconCommand>(&command)) {
      AppendIcon(*icon, density);
    } else if (const auto* glyph = std::get_if<GlyphCommand>(&command)) {
      if (!AppendGlyph(*glyph, density, allow_failure)) {
        return false;
      }
    } else if (const auto* push = std::get_if<PushClipCommand>(&command)) {
      CloseBatch(current());
      clips.push_back(Intersect(current(), DeviceClip(push->rect, density)));
    } else if (std::holds_alternative<PopClipCommand>(command)) {
      CloseBatch(current());
      if (!clips.empty()) {
        clips.pop_back();
      }
    }
  }
  CloseBatch(current());
  return true;
}

absl::Status UiGpuRenderer::Upload(SDL_GPUCommandBuffer* commands) {
  const std::size_t vertex_bytes = vertices_.size() * sizeof(Vertex);
  const bool upload_vertices = vertex_bytes > 0;
  if (upload_vertices) {
    if (absl::Status status = EnsureVertexCapacity(vertex_bytes);
        !status.ok()) {
      return status;
    }
    void* mapped = SDL_MapGPUTransferBuffer(context_->device,
                                            vertex_transfer_.get(), true);
    if (mapped == nullptr) {
      return SdlError("SDL_MapGPUTransferBuffer (ui vertices)");
    }
    const std::span<const Vertex> source(vertices_);
    const std::span<Vertex> destination(static_cast<Vertex*>(mapped),
                                        vertices_.size());
    std::ranges::copy(source, destination.begin());
    SDL_UnmapGPUTransferBuffer(context_->device, vertex_transfer_.get());
  }
  const bool upload_glyphs = atlas_.Dirty();
  if (upload_glyphs) {
    void* mapped =
        SDL_MapGPUTransferBuffer(context_->device, atlas_transfer_.get(), true);
    if (mapped == nullptr) {
      return SdlError("SDL_MapGPUTransferBuffer (ui glyphs)");
    }
    std::ranges::copy(atlas_.Pixels(), std::span<std::uint8_t>(
                                           static_cast<std::uint8_t*>(mapped),
                                           atlas_.Pixels().size())
                                           .begin());
    SDL_UnmapGPUTransferBuffer(context_->device, atlas_transfer_.get());
  }
  if (!upload_vertices && !upload_glyphs && icons_uploaded_) {
    return absl::OkStatus();
  }
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
  if (copy == nullptr) {
    return SdlError("SDL_BeginGPUCopyPass (ui)");
  }
  if (!icons_uploaded_) {
    const SDL_GPUTextureTransferInfo source =
        WholeTransfer(icon_transfer_.get(), kIconAtlasSize);
    const SDL_GPUTextureRegion destination =
        WholeTexture(icon_texture_.get(), kIconAtlasSize);
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    icons_uploaded_ = true;
  }
  if (upload_glyphs) {
    const SDL_GPUTextureTransferInfo source =
        WholeTransfer(atlas_transfer_.get(), kGlyphAtlasSize);
    const SDL_GPUTextureRegion destination =
        WholeTexture(glyph_texture_.get(), kGlyphAtlasSize);
    SDL_UploadToGPUTexture(copy, &source, &destination, true);
    atlas_.ClearDirty();
  }
  if (upload_vertices) {
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = vertex_transfer_.get();
    SDL_GPUBufferRegion destination{};
    destination.buffer = vertex_buffer_.get();
    destination.size = static_cast<Uint32>(vertex_bytes);
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
  }
  SDL_EndGPUCopyPass(copy);
  return absl::OkStatus();
}

absl::Status UiGpuRenderer::Prepare(SDL_GPUCommandBuffer* commands,
                                    const OverlayTarget& target) {
  current_pipeline_ = nullptr;
  if (!Build(target, false)) {
    atlas_.Reset();
    Build(target, true);
  }
  if (vertices_.empty() && icons_uploaded_ && !atlas_.Dirty()) {
    return absl::OkStatus();
  }
  if (!vertices_.empty()) {
    const absl::StatusOr<SDL_GPUGraphicsPipeline*> pipeline =
        PipelineFor(target.format);
    if (!pipeline.ok()) {
      return pipeline.status();
    }
    current_pipeline_ = *pipeline;
  }
  return Upload(commands);
}

void UiGpuRenderer::Draw(SDL_GPUCommandBuffer* commands,
                         SDL_GPURenderPass* pass, const OverlayTarget& target) {
  if (current_pipeline_ == nullptr || batches_.empty()) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, current_pipeline_);
  SDL_GPUViewport viewport{};
  viewport.w = static_cast<float>(target.pixels.width);
  viewport.h = static_cast<float>(target.pixels.height);
  viewport.max_depth = 1.0F;
  SDL_SetGPUViewport(pass, &viewport);
  SDL_GPUBufferBinding vertex_binding{};
  vertex_binding.buffer = vertex_buffer_.get();
  SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
  std::array<SDL_GPUTextureSamplerBinding, kFragmentSamplerCount> samplers{};
  samplers.at(0).texture = glyph_texture_.get();
  samplers.at(0).sampler = sampler_.get();
  samplers.at(1).texture = icon_texture_.get();
  samplers.at(1).sampler = sampler_.get();
  SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), kFragmentSamplerCount);
  const std::array<float, 4> uniform = {viewport.w, viewport.h, 0.0F, 0.0F};
  SDL_PushGPUVertexUniformData(commands, 0, uniform.data(), sizeof(uniform));
  for (const Batch& batch : batches_) {
    SDL_SetGPUScissor(pass, &batch.scissor);
    SDL_DrawGPUPrimitives(pass, batch.count, 1, batch.first, 0);
  }
}

}
