#ifndef ALUMBRA_DESKTOP_UI_COMPOSE_H_
#define ALUMBRA_DESKTOP_UI_COMPOSE_H_

#include <cstdint>
#include <optional>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/strings.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {

struct Interaction {
  std::optional<umbra::ui::ControlId> hover;
  std::optional<umbra::ui::ControlId> pressed;
};

struct FrameInput {
  const umbra::ui::ViewModel* view_model = nullptr;
  const Layout* layout = nullptr;
  const design_tokens::Tokens* tokens = nullptr;
  const TextShaper* shaper = nullptr;
  const StringCatalogue* catalogue = nullptr;
  std::int64_t now_us = 0;
  Interaction interaction;
  float chrome_alpha = 0;
};

[[nodiscard]] DrawList Compose(const FrameInput& input);

[[nodiscard]] std::int64_t ExtrapolatedPosition(
    const umbra::ui::SeekBarSection& seek_bar, std::int64_t now_us);
[[nodiscard]] double PlayheadFraction(const umbra::ui::SeekBarSection& seek_bar,
                                      std::int64_t position_us);

}

#endif
