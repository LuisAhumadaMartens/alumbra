#ifndef ALUMBRA_DESKTOP_UI_INPUT_TRANSLATOR_H_
#define ALUMBRA_DESKTOP_UI_INPUT_TRANSLATOR_H_

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <optional>
#include <string>
#include <vector>

#include "alumbra/desktop_ui/compose.h"
#include "alumbra/desktop_ui/layout.h"
#include "umbra/ui/input.h"

namespace alumbra::desktop_ui {

[[nodiscard]] std::optional<std::string> KeyName(SDL_Keycode key,
                                                 SDL_Keymod modifiers);

class InputTranslator {
 public:
  [[nodiscard]] std::vector<umbra::ui::Input> Translate(
      const SDL_Event& event, const Layout& layout, bool chrome_visible,
      bool fullscreen = false);

  [[nodiscard]] const Interaction& GetInteraction() const {
    return interaction_;
  }
  [[nodiscard]] bool Dragging() const { return drag_ != Drag::kNone; }

 private:
  enum class Drag { kNone, kSeek, kVolume };

  void OnMotion(Point point, const Layout& layout, bool chrome_visible,
                std::vector<umbra::ui::Input>& out);
  void OnButtonDown(Point point, const Layout& layout, bool chrome_visible,
                    std::vector<umbra::ui::Input>& out);
  void OnButtonUp(Point point, const Layout& layout, bool chrome_visible,
                  std::vector<umbra::ui::Input>& out);

  Interaction interaction_;
  Drag drag_ = Drag::kNone;
  bool pointer_in_band_ = false;
  bool press_started_on_video_ = false;
};

}

#endif
