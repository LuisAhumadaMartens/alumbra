#include "alumbra/desktop_ui/text_shaper.h"

#include <hb-ot.h>
#include <hb.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace alumbra::desktop_ui {
namespace {

struct BufferDeleter {
  void operator()(hb_buffer_t* buffer) const { hb_buffer_destroy(buffer); }
};

}

struct TextShaper::Handles {
  Handles() = default;
  ~Handles() {
    if (font != nullptr) {
      hb_font_destroy(font);
    }
    if (face != nullptr) {
      hb_face_destroy(face);
    }
    if (blob != nullptr) {
      hb_blob_destroy(blob);
    }
  }
  Handles(const Handles&) = delete;
  Handles& operator=(const Handles&) = delete;

  hb_blob_t* blob = nullptr;
  hb_face_t* face = nullptr;
  hb_font_t* font = nullptr;
  float units_per_em = 1;
};

absl::StatusOr<std::unique_ptr<TextShaper>> TextShaper::Create(
    std::span<const std::uint8_t> data) {
  auto handles = std::make_unique<Handles>();
  handles->blob = hb_blob_create(reinterpret_cast<const char*>(data.data()),
                                 static_cast<unsigned int>(data.size()),
                                 HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  handles->face = hb_face_create(handles->blob, 0);
  const unsigned int units = hb_face_get_upem(handles->face);
  if (units == 0 || hb_face_get_glyph_count(handles->face) == 0) {
    return absl::InvalidArgumentError("HarfBuzz could not read the font");
  }
  handles->units_per_em = static_cast<float>(units);
  handles->font = hb_font_create(handles->face);
  hb_ot_font_set_funcs(handles->font);
  hb_font_set_scale(handles->font, static_cast<int>(units),
                    static_cast<int>(units));
  return std::unique_ptr<TextShaper>(new TextShaper(std::move(handles)));
}

TextShaper::TextShaper(std::unique_ptr<Handles> handles)
    : handles_(std::move(handles)) {}

TextShaper::~TextShaper() = default;

ShapedText TextShaper::Shape(std::string_view utf8,
                             TextDirection direction) const {
  ShapedText result;
  result.direction = direction;
  const std::unique_ptr<hb_buffer_t, BufferDeleter> buffer(hb_buffer_create());
  hb_buffer_add_utf8(buffer.get(), utf8.data(), static_cast<int>(utf8.size()),
                     0, static_cast<int>(utf8.size()));
  hb_buffer_set_direction(buffer.get(), direction == TextDirection::kLeftToRight
                                            ? HB_DIRECTION_LTR
                                            : HB_DIRECTION_RTL);
  hb_buffer_set_script(buffer.get(), HB_SCRIPT_LATIN);
  hb_buffer_set_language(buffer.get(), hb_language_from_string("en", -1));
  hb_feature_t tabular_numbers{};
  hb_feature_from_string("tnum", -1, &tabular_numbers);
  hb_shape(handles_->font, buffer.get(), &tabular_numbers, 1);
  unsigned int count = 0;
  const hb_glyph_info_t* infos =
      hb_buffer_get_glyph_infos(buffer.get(), &count);
  const hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer.get(), &count);
  const std::span<const hb_glyph_info_t> info_span(infos, count);
  const std::span<const hb_glyph_position_t> position_span(positions, count);
  const float scale = 1.0F / handles_->units_per_em;
  result.glyphs.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    ShapedGlyph glyph;
    glyph.glyph = info_span[index].codepoint;
    glyph.cluster = info_span[index].cluster;
    glyph.x_offset = static_cast<float>(position_span[index].x_offset) * scale;
    glyph.y_offset = static_cast<float>(position_span[index].y_offset) * scale;
    glyph.x_advance =
        static_cast<float>(position_span[index].x_advance) * scale;
    result.advance += glyph.x_advance;
    result.glyphs.push_back(glyph);
  }
  return result;
}

FontMetrics TextShaper::Metrics() const {
  hb_font_extents_t extents{};
  hb_font_get_h_extents(handles_->font, &extents);
  const float scale = 1.0F / handles_->units_per_em;
  FontMetrics metrics;
  metrics.ascent = static_cast<float>(extents.ascender) * scale;
  metrics.descent = -static_cast<float>(extents.descender) * scale;
  metrics.line_gap = static_cast<float>(extents.line_gap) * scale;
  return metrics;
}

}
