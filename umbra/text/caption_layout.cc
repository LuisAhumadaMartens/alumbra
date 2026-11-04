#include "umbra/text/caption_layout.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace umbra::text {
namespace {

constexpr int kMaxAvoidanceSteps = 128;

float Clamp(float value, float low, float high) {
  return std::clamp(value, low, high);
}

CaptionRect SafeRect(const CaptionLayoutInput& input) {
  const float width = std::max(0.0F, input.viewport_width);
  const float height = std::max(0.0F, input.viewport_height);
  const float left = Clamp(input.safe_area.left, 0.0F, width);
  const float right = Clamp(input.safe_area.right, 0.0F, width - left);
  const float top = Clamp(input.safe_area.top, 0.0F, height);
  const float bottom = Clamp(input.safe_area.bottom, 0.0F, height - top);
  return {.x = left,
          .y = top,
          .width = width - left - right,
          .height = height - top - bottom};
}

bool Intersects(const CaptionRect& lhs, const CaptionRect& rhs) {
  return lhs.x < rhs.x + rhs.width && rhs.x < lhs.x + lhs.width &&
         lhs.y < rhs.y + rhs.height && rhs.y < lhs.y + lhs.height;
}

float AlignedStart(float start, float extent, float size,
                   CaptionAlignment alignment) {
  switch (alignment) {
    case CaptionAlignment::kStart:
      return start;
    case CaptionAlignment::kCentre:
      return start + ((extent - size) / 2.0F);
    case CaptionAlignment::kEnd:
      return start + extent - size;
  }
  return start;
}

CaptionRect GridRect(const CaptionCueLayout& cue, const CaptionRect& safe) {
  if (cue.grid_columns <= 0 || cue.grid_rows <= 0) {
    return {};
  }
  const float cell_width = safe.width / static_cast<float>(cue.grid_columns);
  const float cell_height = safe.height / static_cast<float>(cue.grid_rows);
  const int column = std::clamp(cue.grid_column, 0, cue.grid_columns - 1);
  const int row = std::clamp(cue.grid_row, 0, cue.grid_rows - 1);
  const float width =
      Clamp(cue.width, 0.0F,
            cell_width * static_cast<float>(cue.grid_columns - column));
  const float height = Clamp(
      cue.height, 0.0F, cell_height * static_cast<float>(cue.grid_rows - row));
  return {.x = safe.x + (static_cast<float>(column) * cell_width),
          .y = safe.y + (static_cast<float>(row) * cell_height),
          .width = width,
          .height = height};
}

int SnapLineIndex(int line) { return line < 0 ? std::max(0, -line - 1) : line; }

float PercentageOffset(float extent, float percent) {
  return (extent * Clamp(percent, 0.0F, 100.0F)) / 100.0F;
}

CaptionRect HorizontalRect(const CaptionCueLayout& cue, const CaptionRect& safe,
                           float width, float height) {
  CaptionRect rect{.x = 0, .y = 0, .width = width, .height = height};
  const float position =
      safe.x + PercentageOffset(safe.width, cue.position_percent);
  rect.x = AlignedStart(position, 0.0F, width, cue.alignment);
  rect.x = Clamp(rect.x, safe.x, safe.x + safe.width - width);
  if (cue.snap_line.has_value()) {
    const int line = *cue.snap_line;
    const float offset =
        static_cast<float>(SnapLineIndex(line)) * cue.line_height;
    rect.y =
        line < 0 ? safe.y + safe.height - offset - height : safe.y + offset;
  } else if (cue.line_percent.has_value()) {
    rect.y = safe.y + PercentageOffset(safe.height, *cue.line_percent);
    if (cue.bottom_anchored) {
      rect.y -= height;
    }
  } else {
    rect.y = safe.y + safe.height - height;
  }
  rect.y = Clamp(rect.y, safe.y, safe.y + safe.height - height);
  return rect;
}

CaptionRect VerticalRect(const CaptionCueLayout& cue, const CaptionRect& safe,
                         float width, float height) {
  CaptionRect rect{.x = 0, .y = 0, .width = width, .height = height};
  const float position =
      safe.y + PercentageOffset(safe.height, cue.position_percent);
  rect.y = AlignedStart(position, 0.0F, height, cue.alignment);
  rect.y = Clamp(rect.y, safe.y, safe.y + safe.height - height);
  const bool right_to_left =
      cue.writing_mode == CaptionWritingMode::kVerticalRl;
  if (cue.snap_line.has_value()) {
    const int line = *cue.snap_line;
    const float offset =
        static_cast<float>(SnapLineIndex(line)) * cue.line_height;
    rect.x =
        right_to_left ? safe.x + safe.width - offset - width : safe.x + offset;
  } else if (cue.line_percent.has_value()) {
    rect.x = safe.x + PercentageOffset(safe.width, *cue.line_percent);
    if (cue.bottom_anchored) {
      rect.x -= width;
    }
  } else {
    rect.x = right_to_left ? safe.x + safe.width - width : safe.x;
  }
  rect.x = Clamp(rect.x, safe.x, safe.x + safe.width - width);
  return rect;
}

CaptionRect InitialRect(const CaptionCueLayout& cue, const CaptionRect& safe) {
  if (cue.grid_positioned) {
    return GridRect(cue, safe);
  }
  const bool horizontal = cue.writing_mode == CaptionWritingMode::kHorizontal;
  const float size = Clamp(cue.size_percent, 0.0F, 100.0F) / 100.0F;
  const float width =
      Clamp(cue.width, 0.0F, safe.width * (horizontal ? size : 1.0F));
  const float height =
      Clamp(cue.height, 0.0F, safe.height * (horizontal ? 1.0F : size));
  if (horizontal) {
    return HorizontalRect(cue, safe, width, height);
  }
  return VerticalRect(cue, safe, width, height);
}

CaptionRect AvoidOverlaps(CaptionRect rect, const CaptionCueLayout& cue,
                          const CaptionRect& safe,
                          const std::vector<PositionedCaption>& placed,
                          std::span<const CaptionCueLayout> cues) {
  if (cue.grid_positioned || cue.width <= 0 || cue.height <= 0) {
    return rect;
  }
  const bool vertical = cue.writing_mode != CaptionWritingMode::kHorizontal;
  const float step = std::max(1.0F, cue.line_height);
  const bool shift_toward_start =
      cue.region_scroll_up ||
      cue.writing_mode == CaptionWritingMode::kVerticalRl ||
      (cue.writing_mode == CaptionWritingMode::kHorizontal &&
       (cue.snap_line.value_or(-1) < 0 || cue.bottom_anchored));
  for (int count = 0; count < kMaxAvoidanceSteps; ++count) {
    bool collision = false;
    for (const PositionedCaption& item : placed) {
      const CaptionCueLayout& other = cues[item.cue_index];
      if (cue.region_id != 0 && other.region_id != 0 &&
          cue.region_id != other.region_id) {
        continue;
      }
      if (Intersects(rect, item.rect)) {
        collision = true;
        break;
      }
    }
    if (!collision) {
      return rect;
    }
    const float delta = shift_toward_start ? -step : step;
    if (vertical) {
      rect.x += delta;
      rect.x = Clamp(rect.x, safe.x, safe.x + safe.width - rect.width);
    } else {
      rect.y += delta;
      rect.y = Clamp(rect.y, safe.y, safe.y + safe.height - rect.height);
    }
  }
  return rect;
}

}

std::vector<PositionedCaption> LayoutCaptions(const CaptionLayoutInput& input) {
  std::vector<PositionedCaption> result;
  result.reserve(input.cues.size());
  const CaptionRect safe = SafeRect(input);
  for (std::size_t index = 0; index < input.cues.size(); ++index) {
    const CaptionCueLayout& cue = input.cues[index];
    CaptionRect rect = InitialRect(cue, safe);
    rect = AvoidOverlaps(rect, cue, safe, result, input.cues);
    result.push_back({.rect = rect, .cue_index = index});
  }
  return result;
}

}
