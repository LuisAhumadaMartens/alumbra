#ifndef UMBRA_UI_EFFECT_H_
#define UMBRA_UI_EFFECT_H_

#include <cstdint>
#include <string>
#include <variant>

#include "umbra/ui/types.h"

namespace umbra::ui {

struct PlayerPlay {};
struct PlayerPause {};
struct PlayerSeek {
  Micros position_us = 0;
  bool precise = true;
};
struct PlayerSetVolume {
  double volume = 0;
};
struct PlayerSetMuted {
  bool muted = false;
};
struct RequestPresentation {
  PresentationMode mode = PresentationMode::kInline;
};
struct StartTimer {
  TimerKind kind = TimerKind::kAutoHide;
  std::uint64_t generation = 0;
  Micros delay_us = 0;
};
struct BackToApp {};
struct CustomAction {
  std::string name;
};
struct MenuChoice {
  std::string item_id;
};
struct SkipAdRequested {};

using Effect =
    std::variant<PlayerPlay, PlayerPause, PlayerSeek, PlayerSetVolume,
                 PlayerSetMuted, RequestPresentation, StartTimer, BackToApp,
                 CustomAction, MenuChoice, SkipAdRequested>;

struct SequencedEffect {
  std::uint64_t sequence = 0;
  Effect effect;
};

}

#endif
