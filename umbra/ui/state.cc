#include "umbra/ui/state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
#include <vector>

#include "umbra/ui/types.h"

namespace umbra::ui {

bool IsPlaying(const UiState& state) {
  return Is<playback::Playing>(state.playback);
}

bool InAdBreak(const UiState& state) { return Is<ad::Break>(state.ad); }

bool ChromeVisible(const UiState& state) {
  return !Is<chrome::Hidden>(state.chrome);
}

bool MenuIsOpen(const UiState& state) { return Is<menu::Open>(state.menu); }

bool CanSeek(const UiState& state) {
  const bool ready = Is<playback::Paused>(state.playback) ||
                     Is<playback::Playing>(state.playback) ||
                     Is<playback::Buffering>(state.playback) ||
                     Is<playback::Ended>(state.playback);
  return ready && !InAdBreak(state) &&
         state.media.window_end_us > state.media.window_start_us;
}

bool PlaybackAvailable(const UiState& state) {
  return !Is<playback::Idle>(state.playback) &&
         !Is<playback::Error>(state.playback);
}

bool ProviderOwnsInput(const UiState& state) {
  const auto* adbreak = std::get_if<ad::Break>(&state.ad);
  return adbreak != nullptr && adbreak->owner == AdInputOwner::kProvider;
}

Micros ClampToWindow(const Media& media, Micros position_us) {
  if (media.window_end_us > media.window_start_us) {
    return std::clamp(position_us, media.window_start_us, media.window_end_us);
  }
  return std::max<Micros>(position_us, 0);
}

Micros ExtrapolatePlayhead(const Media& media, Micros at_us) {
  const Playhead& head = media.playhead;
  const auto elapsed = static_cast<double>(at_us - head.anchor_us);
  const auto moved = static_cast<Micros>(std::llround(head.rate * elapsed));
  return ClampToWindow(media, head.position_us + moved);
}

double EffectiveRate(const UiState& state) {
  const bool moving = IsPlaying(state) && !state.media.seeking &&
                      !Is<scrub::Scrubbing>(state.scrub);
  return moving ? state.media.rate * state.media.resample_ratio : 0.0;
}

const std::vector<MenuNode>* MenuItems(const UiState& state,
                                       const std::vector<std::uint32_t>& path) {
  const MenuNode* node = state.menu_root.get();
  for (const std::uint32_t index : path) {
    if (node == nullptr || index >= node->children.size() ||
        node->children[index].kind != MenuItemKind::kSubmenu) {
      return nullptr;
    }
    node = &node->children[index];
  }
  return node == nullptr ? nullptr : &node->children;
}

}
