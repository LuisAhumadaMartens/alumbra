#include "umbra/ui/keyboard_map.h"

#include <algorithm>
#include <optional>
#include <string_view>

#include "umbra/ui/input.h"

namespace umbra::ui {

std::optional<Intent> KeyToIntent(std::string_view key) {
  const auto* const found = std::ranges::find_if(
      kKeyboardMap,
      [key](const KeyBinding& binding) { return binding.key == key; });
  if (found == kKeyboardMap.end()) {
    return std::nullopt;
  }
  return found->intent;
}

}
