#ifndef UMBRA_UI_STATE_H_
#define UMBRA_UI_STATE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "umbra/base/error_catalogue.h"
#include "umbra/ui/types.h"

namespace umbra::ui {

struct Playhead {
  Micros position_us = 0;
  double rate = 0;
  Micros anchor_us = 0;
  friend bool operator==(const Playhead&, const Playhead&) = default;
};

struct Media {
  bool paused = true;
  double volume = 1;
  bool muted = false;
  Micros duration_us = 0;
  bool duration_known = false;
  bool live = false;
  Micros window_start_us = 0;
  Micros window_end_us = 0;
  std::vector<TimeRange> buffered;
  double rate = 1;
  double resample_ratio = 1;
  bool seeking = false;
  Playhead playhead;
  Micros value_position_us = 0;
  std::uint64_t value_timer = 0;
};

namespace playback {
struct Idle {};
struct Loading {
  bool spinner = false;
  std::uint64_t timer = 0;
};
struct Playing {};
struct Paused {};
struct Buffering {
  bool spinner = false;
  std::uint64_t timer = 0;
};
struct Ended {};
struct Error {
  base::ErrorCode code = base::ErrorCode::kPlayerInternal;
};
}

namespace chrome {
struct Shown {};
struct AutoHiding {
  Micros deadline_us = 0;
  std::uint64_t timer = 0;
};
struct Pinned {
  bool menu = false;
  bool pointer = false;
  bool scrub = false;
};
struct Hidden {};
}

namespace menu {
struct Closed {};
struct Open {
  std::vector<std::uint32_t> path;
  std::uint32_t highlighted = 0;
};
}

namespace ad {
struct Content {};
struct Break {
  AdInputOwner owner = AdInputOwner::kUi;
  AdSkip skip = AdSkip::kNotSkippable;
};
}

namespace presentation {
struct Inline {};
struct Fullscreen {};
struct PictureInPicture {};
}

namespace scrub {
struct Idle {};
struct Scrubbing {
  double fraction = 0;
  double last_sent = -1;
  bool outstanding = false;
};
}

namespace focus {
struct None {
  ControlId restore = ControlId::kPlayPause;
};
struct On {
  ControlId target = ControlId::kPlayPause;
  ControlId restore = ControlId::kPlayPause;
};
}

using PlaybackRegion =
    std::variant<playback::Idle, playback::Loading, playback::Playing,
                 playback::Paused, playback::Buffering, playback::Ended,
                 playback::Error>;
using ChromeRegion = std::variant<chrome::Shown, chrome::AutoHiding,
                                  chrome::Pinned, chrome::Hidden>;
using MenuRegion = std::variant<menu::Closed, menu::Open>;
using AdRegion = std::variant<ad::Content, ad::Break>;
using PresentationRegion =
    std::variant<presentation::Inline, presentation::Fullscreen,
                 presentation::PictureInPicture>;
using ScrubRegion = std::variant<scrub::Idle, scrub::Scrubbing>;
using FocusRegion = std::variant<focus::None, focus::On>;

struct CustomControl {
  ControlId id = ControlId::kFirstCustom;
  std::string name;
  std::string label_key;
  std::string icon_token;
  Slot slot = Slot::kTrailing;
  bool visible = true;
  bool enabled = true;
  std::optional<bool> pressed;
  friend bool operator==(const CustomControl&, const CustomControl&) = default;
};

struct Announcement {
  std::uint64_t sequence = 0;
  std::string key;
  std::vector<TextArg> args;
  Politeness politeness = Politeness::kPolite;
  friend bool operator==(const Announcement&, const Announcement&) = default;
};

struct UiState {
  Media media;
  PlaybackRegion playback;
  ChromeRegion chrome;
  MenuRegion menu;
  AdRegion ad;
  PresentationRegion presentation;
  ScrubRegion scrub;
  FocusRegion focus;
  std::vector<CustomControl> custom;
  std::shared_ptr<const MenuNode> menu_root;
  std::vector<Announcement> announcements;
  std::uint64_t next_timer = 1;
  std::uint64_t next_announcement = 1;
};

template <class Alternative, class... Ts>
[[nodiscard]] bool Is(const std::variant<Ts...>& region) {
  return std::holds_alternative<Alternative>(region);
}

[[nodiscard]] bool IsPlaying(const UiState& state);
[[nodiscard]] bool InAdBreak(const UiState& state);
[[nodiscard]] bool ChromeVisible(const UiState& state);
[[nodiscard]] bool MenuIsOpen(const UiState& state);
[[nodiscard]] bool CanSeek(const UiState& state);
[[nodiscard]] bool PlaybackAvailable(const UiState& state);
[[nodiscard]] bool ProviderOwnsInput(const UiState& state);
[[nodiscard]] Micros ExtrapolatePlayhead(const Media& media, Micros at_us);
[[nodiscard]] Micros ClampToWindow(const Media& media, Micros position_us);
[[nodiscard]] double EffectiveRate(const UiState& state);
[[nodiscard]] const std::vector<MenuNode>* MenuItems(
    const UiState& state, const std::vector<std::uint32_t>& path);

}

#endif
