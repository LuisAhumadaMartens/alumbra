#ifndef ALUMBRA_DESKTOP_UI_STYLE_H_
#define ALUMBRA_DESKTOP_UI_STYLE_H_

#include <cstdint>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/geometry.h"

namespace alumbra::desktop_ui {

struct ButtonColours {
  Rgba background;
  Rgba foreground;
};

struct Style {
  float tight = 0;
  float small = 0;
  float medium = 0;
  float large = 0;
  float extra_large = 0;
  float target = 0;
  float radius_small = 0;
  float radius_medium = 0;
  float radius_large = 0;
  float radius_pill = 0;
  float button_radius = 0;
  float font_body = 0;
  float font_title = 0;
  float font_caption = 0;
  float disabled_opacity = 0;
  Rgba scrim;
  Rgba elevated;
  Rgba border;
  Rgba text_primary;
  Rgba text_secondary;
  Rgba accent;
  Rgba focus_ring;
  Rgba track;
  Rgba played;
  Rgba buffered;
  ButtonColours idle;
  ButtonColours hover;
  ButtonColours pressed;
  ButtonColours focus;
  ButtonColours disabled;
  std::int64_t fade_us = 0;
  std::int64_t spinner_period_us = 0;
  design_tokens::CubicBezier easing;
};

[[nodiscard]] Style StyleOf(const design_tokens::Tokens& tokens);

}

#endif
