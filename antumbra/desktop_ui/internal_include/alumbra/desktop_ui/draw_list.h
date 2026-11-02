#ifndef ALUMBRA_DESKTOP_UI_DRAW_LIST_H_
#define ALUMBRA_DESKTOP_UI_DRAW_LIST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/icons.h"

namespace alumbra::desktop_ui {

struct RoundedRectCommand {
  Rect rect;
  float radius = 0;
  Rgba fill;
  float border_width = 0;
  Rgba border;
  friend bool operator==(const RoundedRectCommand&,
                         const RoundedRectCommand&) = default;
};

struct IconCommand {
  Rect rect;
  IconId icon = IconId::kDot;
  Rgba color;
  friend bool operator==(const IconCommand&, const IconCommand&) = default;
};

struct GlyphCommand {
  std::uint32_t glyph = 0;
  Point origin;
  float font_size = 0;
  Rgba color;
  friend bool operator==(const GlyphCommand&, const GlyphCommand&) = default;
};

struct PushClipCommand {
  Rect rect;
  friend bool operator==(const PushClipCommand&,
                         const PushClipCommand&) = default;
};

struct PopClipCommand {
  friend bool operator==(const PopClipCommand&,
                         const PopClipCommand&) = default;
};

using DrawCommand = std::variant<RoundedRectCommand, IconCommand, GlyphCommand,
                                 PushClipCommand, PopClipCommand>;

class DrawList {
 public:
  void FillRoundedRect(const Rect& rect, float radius, const Rgba& fill);
  void StrokeRoundedRect(const Rect& rect, float radius, float width,
                         const Rgba& border);
  void FillAndStrokeRoundedRect(const Rect& rect, float radius,
                                const Rgba& fill, float width,
                                const Rgba& border);
  void DrawIcon(const Rect& rect, IconId icon, const Rgba& color);
  void DrawGlyph(std::uint32_t glyph, Point origin, float font_size,
                 const Rgba& color);
  void PushClip(const Rect& rect);
  void PopClip();

  [[nodiscard]] const std::vector<DrawCommand>& Commands() const {
    return commands_;
  }
  [[nodiscard]] std::size_t Size() const { return commands_.size(); }
  [[nodiscard]] bool Empty() const { return commands_.empty(); }
  [[nodiscard]] std::string Describe() const;

  friend bool operator==(const DrawList&, const DrawList&) = default;

 private:
  std::vector<DrawCommand> commands_;
};

}

#endif
