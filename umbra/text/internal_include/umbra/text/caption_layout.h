#ifndef UMBRA_TEXT_CAPTION_LAYOUT_H_
#define UMBRA_TEXT_CAPTION_LAYOUT_H_

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace umbra::text {

enum class CaptionWritingMode {
  kHorizontal,
  kVerticalRl,
  kVerticalLr,
};

enum class CaptionAlignment {
  kStart,
  kCentre,
  kEnd,
};

struct CaptionRect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  friend bool operator==(const CaptionRect&, const CaptionRect&) = default;
};

struct CaptionInsets {
  float top = 0;
  float right = 0;
  float bottom = 0;
  float left = 0;
};

struct CaptionCueLayout {
  float width = 0;
  float height = 0;
  float line_height = 0;
  float position_percent = 50;
  float size_percent = 100;
  std::optional<float> line_percent;
  std::optional<int> snap_line;
  CaptionAlignment alignment = CaptionAlignment::kCentre;
  CaptionWritingMode writing_mode = CaptionWritingMode::kHorizontal;
  int region_id = 0;
  bool region_scroll_up = false;
  bool bottom_anchored = false;
  bool grid_positioned = false;
  int grid_column = 0;
  int grid_row = 0;
  int grid_columns = 32;
  int grid_rows = 15;
};

struct CaptionLayoutInput {
  float viewport_width = 0;
  float viewport_height = 0;
  CaptionInsets safe_area;
  std::span<const CaptionCueLayout> cues;
};

struct PositionedCaption {
  CaptionRect rect;
  std::size_t cue_index = 0;
};

[[nodiscard]] std::vector<PositionedCaption> LayoutCaptions(
    const CaptionLayoutInput& input);

}

#endif
