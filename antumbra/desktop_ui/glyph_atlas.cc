#include "alumbra/desktop_ui/glyph_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/structure.h"

namespace alumbra::desktop_ui {
namespace {

constexpr float kSizeQuantum = 64.0F;

}

GlyphAtlas::GlyphAtlas(int size)
    : size_(size),
      pixels_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
              0) {}

void GlyphAtlas::Reset() {
  glyphs_.clear();
  std::ranges::fill(pixels_, std::uint8_t{0});
  cursor_x_ = 0;
  cursor_y_ = 0;
  row_height_ = 0;
  dirty_ = true;
}

bool GlyphAtlas::Reserve(int width, int height, int& x, int& y) {
  const int padded_width = width + (2 * kGlyphPadding);
  const int padded_height = height + (2 * kGlyphPadding);
  if (padded_width > size_ || padded_height > size_) {
    return false;
  }
  if (cursor_x_ + padded_width > size_) {
    cursor_x_ = 0;
    cursor_y_ += row_height_;
    row_height_ = 0;
  }
  if (cursor_y_ + padded_height > size_) {
    return false;
  }
  x = cursor_x_ + kGlyphPadding;
  y = cursor_y_ + kGlyphPadding;
  cursor_x_ += padded_width;
  row_height_ = std::max(row_height_, padded_height);
  return true;
}

std::optional<AtlasGlyph> GlyphAtlas::Lookup(const FontFace& font,
                                             std::uint32_t glyph,
                                             float pixel_size) {
  const Key key{
      glyph, static_cast<std::int32_t>(std::lround(pixel_size * kSizeQuantum))};
  const auto found = glyphs_.find(key);
  if (found != glyphs_.end()) {
    return found->second;
  }
  const std::optional<GlyphBitmap> bitmap = font.Rasterise(glyph, pixel_size);
  if (!bitmap.has_value()) {
    return std::nullopt;
  }
  AtlasGlyph entry;
  entry.width = bitmap->width;
  entry.height = bitmap->height;
  entry.left = bitmap->left;
  entry.top = bitmap->top;
  if (bitmap->width > 0 && bitmap->height > 0) {
    int x = 0;
    int y = 0;
    if (!Reserve(bitmap->width, bitmap->height, x, y)) {
      return std::nullopt;
    }
    const auto atlas = static_cast<float>(size_);
    for (int row = 0; row < bitmap->height; ++row) {
      for (int column = 0; column < bitmap->width; ++column) {
        pixels_.at((static_cast<std::size_t>(y + row) *
                    static_cast<std::size_t>(size_)) +
                   static_cast<std::size_t>(x + column)) =
            bitmap->pixels.at((static_cast<std::size_t>(row) *
                               static_cast<std::size_t>(bitmap->width)) +
                              static_cast<std::size_t>(column));
      }
    }
    entry.u0 = static_cast<float>(x) / atlas;
    entry.v0 = static_cast<float>(y) / atlas;
    entry.u1 = static_cast<float>(x + bitmap->width) / atlas;
    entry.v1 = static_cast<float>(y + bitmap->height) / atlas;
    dirty_ = true;
  }
  glyphs_.emplace(key, entry);
  return entry;
}

}
