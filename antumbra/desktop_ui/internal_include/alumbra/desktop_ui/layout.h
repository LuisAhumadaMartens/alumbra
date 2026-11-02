#ifndef ALUMBRA_DESKTOP_UI_LAYOUT_H_
#define ALUMBRA_DESKTOP_UI_LAYOUT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/icons.h"
#include "alumbra/desktop_ui/strings.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {

struct ControlLayout {
  umbra::ui::ControlId id = umbra::ui::ControlId::kPlayPause;
  umbra::ui::ControlKind kind = umbra::ui::ControlKind::kButton;
  Rect hit;
  Rect icon;
  Rect track;
  IconId icon_id = IconId::kDot;
  bool slider = false;
};

struct TextBlock {
  std::string text;
  ShapedText shaped;
  Point origin;
  float font_size = 0;
};

struct ErrorLayout {
  Rect panel;
  Rect icon;
  TextBlock title;
  TextBlock message;
};

struct SpinnerLayout {
  Point centre;
  float radius = 0;
  float dot = 0;
};

struct Layout {
  Size viewport;
  Rect band;
  std::vector<ControlLayout> controls;
  std::optional<Rect> time_box;
  float time_baseline = 0;
  float time_font_size = 0;
  std::optional<ErrorLayout> error;
  std::optional<SpinnerLayout> spinner;

  [[nodiscard]] const ControlLayout* Find(umbra::ui::ControlId id) const;
  [[nodiscard]] std::optional<umbra::ui::ControlId> HitTest(Point point) const;
  [[nodiscard]] bool InBand(Point point) const { return band.Contains(point); }
};

struct LayoutInput {
  Size viewport;
  const umbra::ui::ViewModel* view_model = nullptr;
  const design_tokens::Tokens* tokens = nullptr;
  const TextShaper* shaper = nullptr;
  const StringCatalogue* catalogue = nullptr;
};

[[nodiscard]] Layout ComputeLayout(const LayoutInput& input);

[[nodiscard]] double SliderFraction(const ControlLayout& control, float x);
[[nodiscard]] std::string ClockText(const umbra::ui::SeekBarSection& seek_bar,
                                    std::int64_t position_us,
                                    const StringCatalogue& catalogue,
                                    bool reserve);

}

#endif
