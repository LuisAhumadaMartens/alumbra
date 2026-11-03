#ifndef ALUMBRA_DESKTOP_UI_TEXT_SHAPER_H_
#define ALUMBRA_DESKTOP_UI_TEXT_SHAPER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace alumbra::desktop_ui {

enum class TextDirection { kLeftToRight, kRightToLeft };

struct ShapedGlyph {
  std::uint32_t glyph = 0;
  std::uint32_t cluster = 0;
  float x_offset = 0;
  float y_offset = 0;
  float x_advance = 0;
};

struct ShapedText {
  std::vector<ShapedGlyph> glyphs;
  float advance = 0;
  TextDirection direction = TextDirection::kLeftToRight;
};

struct FontMetrics {
  float ascent = 0;
  float descent = 0;
  float line_gap = 0;
};

class TextShaper {
 public:
  static absl::StatusOr<std::unique_ptr<TextShaper>> Create(
      std::span<const std::uint8_t> data);

  ~TextShaper();
  TextShaper(const TextShaper&) = delete;
  TextShaper& operator=(const TextShaper&) = delete;

  [[nodiscard]] ShapedText Shape(std::string_view utf8,
                                 TextDirection direction) const;
  [[nodiscard]] FontMetrics Metrics() const;

 private:
  struct Handles;

  explicit TextShaper(std::unique_ptr<Handles> handles);

  std::unique_ptr<Handles> handles_;
};

}

#endif
