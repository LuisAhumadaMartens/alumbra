#ifndef UMBRA_UI_KEYBOARD_MAP_H_
#define UMBRA_UI_KEYBOARD_MAP_H_

#include <array>
#include <optional>
#include <string_view>

#include "umbra/ui/input.h"

namespace umbra::ui {

struct KeyBinding {
  std::string_view key;
  Intent intent;
};

inline constexpr std::array<KeyBinding, 17> kKeyboardMap{{
    {.key = "Space", .intent = TogglePlay{}},
    {.key = "k", .intent = TogglePlay{}},
    {.key = "j", .intent = SeekSteps{.steps = -2}},
    {.key = "l", .intent = SeekSteps{.steps = 2}},
    {.key = "ArrowLeft", .intent = Navigate{.direction = Direction::kLeft}},
    {.key = "ArrowRight", .intent = Navigate{.direction = Direction::kRight}},
    {.key = "ArrowUp", .intent = Navigate{.direction = Direction::kUp}},
    {.key = "ArrowDown", .intent = Navigate{.direction = Direction::kDown}},
    {.key = "m", .intent = ToggleMute{}},
    {.key = "f", .intent = ToggleFullscreen{}},
    {.key = "Enter", .intent = ActivateFocused{}},
    {.key = "Escape", .intent = Back{}},
    {.key = "Tab", .intent = FocusTraverse{.backward = false}},
    {.key = "Shift+Tab", .intent = FocusTraverse{.backward = true}},
    {.key = "MediaPlayPause", .intent = TogglePlay{}},
    {.key = "MediaPlay", .intent = Play{}},
    {.key = "MediaPause", .intent = Pause{}},
}};

[[nodiscard]] std::optional<Intent> KeyToIntent(std::string_view key);

}

#endif
