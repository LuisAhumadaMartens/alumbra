#ifndef ALUMBRA_DESKTOP_UI_GLYPH_ATLAS_H_
#define ALUMBRA_DESKTOP_UI_GLYPH_ATLAS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "alumbra/desktop_ui/font_face.h"

namespace alumbra::desktop_ui {

struct AtlasGlyph {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  float u0 = 0;
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
};

class GlyphAtlas {
 public:
  explicit GlyphAtlas(int size);

  [[nodiscard]] std::optional<AtlasGlyph> Lookup(const FontFace& font,
                                                 std::uint32_t glyph,
                                                 float pixel_size);
  void Reset();

  [[nodiscard]] int Size() const { return size_; }
  [[nodiscard]] const std::vector<std::uint8_t>& Pixels() const {
    return pixels_;
  }
  [[nodiscard]] bool Dirty() const { return dirty_; }
  void ClearDirty() { dirty_ = false; }
  [[nodiscard]] std::size_t GlyphCount() const { return glyphs_.size(); }

 private:
  using Key = std::pair<std::uint32_t, std::int32_t>;

  bool Reserve(int width, int height, int& x, int& y);

  int size_;
  std::vector<std::uint8_t> pixels_;
  std::map<Key, AtlasGlyph> glyphs_;
  int cursor_x_ = 0;
  int cursor_y_ = 0;
  int row_height_ = 0;
  bool dirty_ = false;
};

}

#endif
