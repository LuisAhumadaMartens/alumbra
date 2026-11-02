#include "alumbra/desktop_ui/draw_list.h"

#include <cstdint>
#include <format>
#include <string>
#include <variant>

#include "absl/strings/str_cat.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/icons.h"

namespace alumbra::desktop_ui {
namespace {

std::string Format(const Rect& rect) {
  return std::format("[{:.2f} {:.2f} {:.2f} {:.2f}]", rect.x, rect.y,
                     rect.width, rect.height);
}

std::string Format(const Rgba& color) {
  return std::format("#{:.3f},{:.3f},{:.3f},{:.3f}", color.red, color.green,
                     color.blue, color.alpha);
}

struct Describer {
  std::string operator()(const RoundedRectCommand& command) const {
    return absl::StrCat("rrect ", Format(command.rect),
                        " r=", std::format("{:.2f}", command.radius),
                        " fill=", Format(command.fill),
                        " border=", std::format("{:.2f}", command.border_width),
                        " ", Format(command.border));
  }
  std::string operator()(const IconCommand& command) const {
    return absl::StrCat("icon ", Format(command.rect),
                        " id=", static_cast<int>(command.icon), " ",
                        Format(command.color));
  }
  std::string operator()(const GlyphCommand& command) const {
    return absl::StrCat(
        "glyph ", command.glyph, " at ",
        std::format("{:.2f},{:.2f}", command.origin.x, command.origin.y),
        " size=", std::format("{:.2f}", command.font_size), " ",
        Format(command.color));
  }
  std::string operator()(const PushClipCommand& command) const {
    return absl::StrCat("clip ", Format(command.rect));
  }
  std::string operator()([[maybe_unused]] const PopClipCommand& command) const {
    return "unclip";
  }
};

}

void DrawList::FillRoundedRect(const Rect& rect, float radius,
                               const Rgba& fill) {
  commands_.emplace_back(RoundedRectCommand{.rect = rect,
                                            .radius = radius,
                                            .fill = fill,
                                            .border_width = 0.0F,
                                            .border = Rgba{}});
}

void DrawList::StrokeRoundedRect(const Rect& rect, float radius, float width,
                                 const Rgba& border) {
  commands_.emplace_back(RoundedRectCommand{.rect = rect,
                                            .radius = radius,
                                            .fill = Rgba{},
                                            .border_width = width,
                                            .border = border});
}

void DrawList::FillAndStrokeRoundedRect(const Rect& rect, float radius,
                                        const Rgba& fill, float width,
                                        const Rgba& border) {
  commands_.emplace_back(RoundedRectCommand{.rect = rect,
                                            .radius = radius,
                                            .fill = fill,
                                            .border_width = width,
                                            .border = border});
}

void DrawList::DrawIcon(const Rect& rect, IconId icon, const Rgba& color) {
  commands_.emplace_back(
      IconCommand{.rect = rect, .icon = icon, .color = color});
}

void DrawList::DrawGlyph(std::uint32_t glyph, Point origin, float font_size,
                         const Rgba& color) {
  commands_.emplace_back(GlyphCommand{.glyph = glyph,
                                      .origin = origin,
                                      .font_size = font_size,
                                      .color = color});
}

void DrawList::PushClip(const Rect& rect) {
  commands_.emplace_back(PushClipCommand{rect});
}

void DrawList::PopClip() { commands_.emplace_back(PopClipCommand{}); }

std::string DrawList::Describe() const {
  std::string text;
  for (const DrawCommand& command : commands_) {
    absl::StrAppend(&text, std::visit(Describer{}, command), "\n");
  }
  return text;
}

}
