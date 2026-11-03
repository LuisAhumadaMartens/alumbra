#include "alumbra/desktop_ui/input_translator.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <optional>
#include <string>
#include <vector>

#include "alumbra/desktop_ui/compose.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/layout.h"
#include "umbra/ui/input.h"
#include "umbra/ui/keyboard_map.h"
#include "umbra/ui/types.h"

namespace alumbra::desktop_ui {
namespace {

using umbra::ui::ControlId;
using umbra::ui::Input;

template <typename T>
const T& EventAs(const SDL_Event& event) {
  return *reinterpret_cast<const T*>(&event);
}

bool IsAdjustingModifier(SDL_Keymod modifiers) {
  return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
}

}

std::optional<std::string> KeyName(SDL_Keycode key, SDL_Keymod modifiers) {
  if (IsAdjustingModifier(modifiers)) {
    return std::nullopt;
  }
  switch (key) {
    case SDLK_SPACE:
      return "Space";
    case SDLK_LEFT:
      return "ArrowLeft";
    case SDLK_RIGHT:
      return "ArrowRight";
    case SDLK_UP:
      return "ArrowUp";
    case SDLK_DOWN:
      return "ArrowDown";
    case SDLK_RETURN:
    case SDLK_RETURN2:
    case SDLK_KP_ENTER:
      return "Enter";
    case SDLK_ESCAPE:
      return "Escape";
    case SDLK_TAB:
      return (modifiers & SDL_KMOD_SHIFT) != 0 ? "Shift+Tab" : "Tab";
    case SDLK_MEDIA_PLAY_PAUSE:
      return "MediaPlayPause";
    case SDLK_MEDIA_PLAY:
      return "MediaPlay";
    case SDLK_MEDIA_PAUSE:
      return "MediaPause";
    default:
      break;
  }
  if (key >= SDLK_A && key <= SDLK_Z) {
    return std::string(1, static_cast<char>(key));
  }
  return std::nullopt;
}

void InputTranslator::OnMotion(Point point, const Layout& layout,
                               bool chrome_visible, std::vector<Input>& out) {
  if (!layout.provider_reserved.Empty() &&
      layout.provider_reserved.Contains(point)) {
    interaction_.hover = std::nullopt;
    if (pointer_in_band_) {
      pointer_in_band_ = false;
      out.emplace_back(umbra::ui::Intent{umbra::ui::PointerLeft{}});
    }
    return;
  }
  out.emplace_back(umbra::ui::Intent{umbra::ui::PointerActivity{}});
  interaction_.hover =
      chrome_visible ? layout.HitTest(point) : std::optional<ControlId>();
  const bool in_band = chrome_visible && layout.InBand(point);
  if (in_band != pointer_in_band_) {
    pointer_in_band_ = in_band;
    if (in_band) {
      out.emplace_back(umbra::ui::Intent{umbra::ui::PointerEntered{}});
    } else {
      out.emplace_back(umbra::ui::Intent{umbra::ui::PointerLeft{}});
    }
  }
  if (drag_ == Drag::kSeek) {
    if (const ControlLayout* seek = layout.Find(ControlId::kSeekBar)) {
      out.emplace_back(umbra::ui::Intent{
          umbra::ui::ScrubMove{.fraction = SliderFraction(*seek, point.x)}});
    }
  } else if (drag_ == Drag::kVolume) {
    if (const ControlLayout* volume = layout.Find(ControlId::kVolume)) {
      out.emplace_back(umbra::ui::Intent{
          umbra::ui::SetVolume{.volume = SliderFraction(*volume, point.x)}});
    }
  }
}

void InputTranslator::OnButtonDown(Point point, const Layout& layout,
                                   bool chrome_visible,
                                   std::vector<Input>& out) {
  if (!layout.provider_reserved.Empty() &&
      layout.provider_reserved.Contains(point)) {
    interaction_.pressed = std::nullopt;
    press_started_on_video_ = false;
    drag_ = Drag::kNone;
    return;
  }
  out.emplace_back(umbra::ui::Intent{umbra::ui::PointerActivity{}});
  interaction_.pressed = std::nullopt;
  press_started_on_video_ = false;
  const std::optional<ControlId> hit =
      chrome_visible ? layout.HitTest(point) : std::optional<ControlId>();
  interaction_.hover = hit;
  if (!hit.has_value()) {
    press_started_on_video_ = !(chrome_visible && layout.InBand(point));
    return;
  }
  const ControlLayout* control = layout.Find(*hit);
  if (control == nullptr) {
    return;
  }
  if (*hit == ControlId::kSeekBar) {
    drag_ = Drag::kSeek;
    out.emplace_back(umbra::ui::Intent{
        umbra::ui::ScrubBegin{.fraction = SliderFraction(*control, point.x)}});
  } else if (control->slider) {
    drag_ = Drag::kVolume;
    out.emplace_back(umbra::ui::Intent{
        umbra::ui::SetVolume{.volume = SliderFraction(*control, point.x)}});
  } else {
    interaction_.pressed = hit;
  }
}

void InputTranslator::OnButtonUp(Point point, const Layout& layout,
                                 bool chrome_visible, std::vector<Input>& out) {
  if (!layout.provider_reserved.Empty() &&
      layout.provider_reserved.Contains(point)) {
    drag_ = Drag::kNone;
    interaction_.pressed = std::nullopt;
    press_started_on_video_ = false;
    return;
  }
  const std::optional<ControlId> hit =
      chrome_visible ? layout.HitTest(point) : std::optional<ControlId>();
  if (drag_ == Drag::kSeek) {
    out.emplace_back(umbra::ui::Intent{umbra::ui::ScrubEnd{}});
  } else if (interaction_.pressed.has_value() && hit == interaction_.pressed) {
    out.emplace_back(
        umbra::ui::Intent{umbra::ui::Activate{.id = *interaction_.pressed}});
  } else if (press_started_on_video_ && !hit.has_value() &&
             !(chrome_visible && layout.InBand(point))) {
    out.emplace_back(umbra::ui::Intent{umbra::ui::TogglePlay{}});
  }
  drag_ = Drag::kNone;
  interaction_.pressed = std::nullopt;
  press_started_on_video_ = false;
}

std::vector<Input> InputTranslator::Translate(const SDL_Event& event,
                                              const Layout& layout,
                                              bool chrome_visible) {
  std::vector<Input> out;
  switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      const auto& motion = EventAs<SDL_MouseMotionEvent>(event);
      OnMotion(Point{motion.x, motion.y}, layout, chrome_visible, out);
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      const auto& button = EventAs<SDL_MouseButtonEvent>(event);
      if (button.button == SDL_BUTTON_LEFT) {
        OnButtonDown(Point{button.x, button.y}, layout, chrome_visible, out);
      }
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      const auto& button = EventAs<SDL_MouseButtonEvent>(event);
      if (button.button == SDL_BUTTON_LEFT) {
        OnButtonUp(Point{button.x, button.y}, layout, chrome_visible, out);
      }
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      const auto& wheel = EventAs<SDL_MouseWheelEvent>(event);
      if (wheel.y != 0) {
        out.emplace_back(umbra::ui::Intent{
            umbra::ui::VolumeSteps{.steps = wheel.y > 0 ? 1 : -1}});
      }
      break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      interaction_.hover = std::nullopt;
      if (pointer_in_band_) {
        pointer_in_band_ = false;
        out.emplace_back(umbra::ui::Intent{umbra::ui::PointerLeft{}});
      }
      break;
    case SDL_EVENT_KEY_DOWN: {
      const auto& key = EventAs<SDL_KeyboardEvent>(event);
      const std::optional<std::string> name = KeyName(key.key, key.mod);
      if (name.has_value()) {
        if (const std::optional<umbra::ui::Intent> intent =
                umbra::ui::KeyToIntent(*name)) {
          out.emplace_back(*intent);
        }
      }
      break;
    }
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
      out.emplace_back(umbra::ui::EngineEvent{umbra::ui::PresentationChanged{
          .mode = umbra::ui::PresentationMode::kFullscreen}});
      break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
      out.emplace_back(umbra::ui::EngineEvent{umbra::ui::PresentationChanged{
          .mode = umbra::ui::PresentationMode::kInline}});
      break;
    default:
      break;
  }
  return out;
}

}
