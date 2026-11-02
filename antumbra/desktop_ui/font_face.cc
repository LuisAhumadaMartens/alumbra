#include "alumbra/desktop_ui/font_face.h"

#include <freetype/freetype.h>
#include <freetype/ftimage.h>
#include <freetype/fttypes.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace alumbra::desktop_ui {
namespace {

constexpr FT_F26Dot6 kSubpixelUnits = 64;
constexpr FT_UInt kBaseResolution = 72;

}

struct FontFace::Handles {
  Handles() = default;
  ~Handles() {
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
  }
  Handles(const Handles&) = delete;
  Handles& operator=(const Handles&) = delete;

  FT_Library library = nullptr;
  FT_Face face = nullptr;
};

absl::StatusOr<std::unique_ptr<FontFace>> FontFace::Create(
    std::span<const std::uint8_t> data) {
  auto handles = std::make_unique<Handles>();
  if (FT_Init_FreeType(&handles->library) != 0) {
    return absl::InternalError("FreeType failed to initialise");
  }
  if (FT_New_Memory_Face(handles->library, data.data(),
                         static_cast<FT_Long>(data.size()), 0,
                         &handles->face) != 0) {
    return absl::InvalidArgumentError("FreeType could not open the font");
  }
  return std::unique_ptr<FontFace>(new FontFace(std::move(handles)));
}

FontFace::FontFace(std::unique_ptr<Handles> handles)
    : handles_(std::move(handles)) {}

FontFace::~FontFace() = default;

std::optional<GlyphBitmap> FontFace::Rasterise(std::uint32_t glyph,
                                               float pixel_size) const {
  FT_Face face = handles_->face;
  const auto size = static_cast<FT_F26Dot6>(
      std::lround(pixel_size * static_cast<float>(kSubpixelUnits)));
  if (size <= 0 ||
      FT_Set_Char_Size(face, 0, size, kBaseResolution, kBaseResolution) != 0) {
    return std::nullopt;
  }
  if (FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
    return std::nullopt;
  }
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
    return std::nullopt;
  }
  const FT_Bitmap& bitmap = face->glyph->bitmap;
  GlyphBitmap result;
  result.width = static_cast<int>(bitmap.width);
  result.height = static_cast<int>(bitmap.rows);
  result.left = face->glyph->bitmap_left;
  result.top = face->glyph->bitmap_top;
  result.pixels.resize(static_cast<std::size_t>(result.width) *
                       static_cast<std::size_t>(result.height));
  const auto pitch = static_cast<std::size_t>(std::abs(bitmap.pitch));
  const auto width = static_cast<std::size_t>(result.width);
  const auto rows = static_cast<std::size_t>(result.height);
  const std::span<const std::uint8_t> source(bitmap.buffer, pitch * rows);
  for (std::size_t row = 0; row < rows; ++row) {
    const std::span<const std::uint8_t> line =
        source.subspan(row * pitch, width);
    for (std::size_t column = 0; column < width; ++column) {
      result.pixels.at((row * width) + column) = line[column];
    }
  }
  return result;
}

}
