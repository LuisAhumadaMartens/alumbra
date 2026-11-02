#ifndef ALUMBRA_DESKTOP_UI_FONT_FACE_H_
#define ALUMBRA_DESKTOP_UI_FONT_FACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "absl/status/statusor.h"

namespace alumbra::desktop_ui {

struct GlyphBitmap {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  std::vector<std::uint8_t> pixels;
};

class FontFace {
 public:
  static absl::StatusOr<std::unique_ptr<FontFace>> Create(
      std::span<const std::uint8_t> data);

  ~FontFace();
  FontFace(const FontFace&) = delete;
  FontFace& operator=(const FontFace&) = delete;

  [[nodiscard]] std::optional<GlyphBitmap> Rasterise(std::uint32_t glyph,
                                                     float pixel_size) const;

 private:
  struct Handles;

  explicit FontFace(std::unique_ptr<Handles> handles);

  std::unique_ptr<Handles> handles_;
};

}

#endif
