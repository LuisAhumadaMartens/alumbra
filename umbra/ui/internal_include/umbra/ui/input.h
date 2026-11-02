#ifndef UMBRA_UI_INPUT_H_
#define UMBRA_UI_INPUT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "umbra/base/error_catalogue.h"
#include "umbra/ui/types.h"

namespace umbra::ui {

struct TogglePlay {};
struct Play {};
struct Pause {};
struct SeekSteps {
  int steps = 0;
};
struct SeekToFraction {
  double fraction = 0;
};
struct ScrubBegin {
  double fraction = 0;
};
struct ScrubMove {
  double fraction = 0;
};
struct ScrubEnd {};
struct ScrubCancel {};
struct SetVolume {
  double volume = 0;
};
struct VolumeSteps {
  int steps = 0;
};
struct ToggleMute {};
struct ToggleFullscreen {};
struct ShowControls {};
struct HideControls {};
struct ToggleControls {};
struct PointerActivity {};
struct PointerEntered {};
struct PointerLeft {};
struct Navigate {
  Direction direction = Direction::kLeft;
};
struct Back {};
struct ActivateFocused {};
struct Activate {
  ControlId id = ControlId::kPlayPause;
};
struct FocusControl {
  ControlId id = ControlId::kPlayPause;
};
struct FocusTraverse {
  bool backward = false;
};
struct FocusChrome {};
struct MoveFocus {
  Direction direction = Direction::kLeft;
};
struct OpenMenu {};
struct MenuNavigate {
  Direction direction = Direction::kDown;
};
struct MenuBack {};
struct MenuActivate {};
struct SkipAd {};
struct ActivateCustom {
  ControlId id = ControlId::kFirstCustom;
};
struct PassBack {};

using Intent =
    std::variant<TogglePlay, Play, Pause, SeekSteps, SeekToFraction, ScrubBegin,
                 ScrubMove, ScrubEnd, ScrubCancel, SetVolume, VolumeSteps,
                 ToggleMute, ToggleFullscreen, ShowControls, HideControls,
                 ToggleControls, PointerActivity, PointerEntered, PointerLeft,
                 Navigate, Back, ActivateFocused, Activate, FocusControl,
                 FocusTraverse, FocusChrome, MoveFocus, OpenMenu, MenuNavigate,
                 MenuBack, MenuActivate, SkipAd, ActivateCustom, PassBack>;

struct LoadStarted {};
struct SourceCleared {};
struct CanPlay {};
struct PlaybackStarted {};
struct PlaybackPaused {};
struct Waiting {};
struct Seeking {};
struct Seeked {
  Micros position_us = 0;
};
struct TimeUpdate {
  Micros position_us = 0;
  Micros anchor_us = 0;
};
struct DurationChanged {
  Micros duration_us = 0;
  bool live = false;
};
struct SeekableChanged {
  Micros start_us = 0;
  Micros end_us = 0;
};
struct BufferedChanged {
  std::vector<TimeRange> ranges;
};
struct VolumeChanged {
  double volume = 1;
  bool muted = false;
};
struct RateChanged {
  double rate = 1;
  double resample_ratio = 1;
};
struct PlaybackEnded {};
struct PlaybackFailed {
  base::ErrorCode code = base::ErrorCode::kPlayerInternal;
};
struct PlayRejected {};
struct PresentationChanged {
  PresentationMode mode = PresentationMode::kInline;
};
struct AdBreakStarted {
  AdInputOwner owner = AdInputOwner::kUi;
  bool skippable = false;
};
struct AdSkipAvailable {};
struct AdBreakEnded {};

using EngineEvent =
    std::variant<LoadStarted, SourceCleared, CanPlay, PlaybackStarted,
                 PlaybackPaused, Waiting, Seeking, Seeked, TimeUpdate,
                 DurationChanged, SeekableChanged, BufferedChanged,
                 VolumeChanged, RateChanged, PlaybackEnded, PlaybackFailed,
                 PlayRejected, PresentationChanged, AdBreakStarted,
                 AdSkipAvailable, AdBreakEnded>;

struct RegisterControl {
  std::string name;
  std::string label_key;
  std::string icon_token;
  Slot slot = Slot::kTrailing;
};
struct UpdateControl {
  std::string name;
  std::optional<bool> visible;
  std::optional<bool> enabled;
  std::optional<bool> pressed;
};
struct SetMenu {
  std::shared_ptr<const MenuNode> root;
};

using CatalogChange = std::variant<RegisterControl, UpdateControl, SetMenu>;

struct TimerFired {
  TimerKind kind = TimerKind::kAutoHide;
  std::uint64_t generation = 0;
};

using Input = std::variant<Intent, EngineEvent, CatalogChange, TimerFired>;

}

#endif
