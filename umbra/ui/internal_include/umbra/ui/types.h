#ifndef UMBRA_UI_TYPES_H_
#define UMBRA_UI_TYPES_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace umbra::ui {

using Micros = std::int64_t;

inline constexpr Micros kValueTextIntervalUs = 5'000'000;
inline constexpr Micros kPlayheadToleranceUs = 150'000;

enum class ControlId : std::uint16_t {
  kPlayPause = 1,
  kSeekBar = 2,
  kMute = 3,
  kVolume = 4,
  kSettings = 5,
  kFullscreen = 6,
  kSkipAd = 7,
  kFirstCustom = 64,
};

enum class Direction { kUp, kDown, kLeft, kRight };
enum class ControlKind {
  kButton,
  kToggleButton,
  kSlider,
  kMenuButton,
  kCustom
};
enum class Role {
  kButton,
  kSlider,
  kMenu,
  kMenuItem,
  kMenuItemRadio,
  kMenuItemCheckbox
};
enum class Slot { kProgress, kLeading, kTrailing };
enum class Layout { kDesktop, kMobile, kTv };
enum class Politeness { kPolite, kAssertive };
enum class AdInputOwner { kUi, kProvider };
enum class AdSkip { kNotSkippable, kWaiting, kAvailable };
enum class PresentationMode { kInline, kFullscreen, kPictureInPicture };
enum class TimerKind { kAutoHide, kSpinner, kValueText };
enum class MenuItemKind { kSubmenu, kChoice, kToggle };

struct TimeRange {
  Micros start_us = 0;
  Micros end_us = 0;
  friend bool operator==(const TimeRange&, const TimeRange&) = default;
};

struct TextArg {
  std::string name;
  std::variant<std::int64_t, double, std::string> value;
  friend bool operator==(const TextArg&, const TextArg&) = default;
};

struct MenuNode {
  std::string id;
  std::string label_key;
  MenuItemKind kind = MenuItemKind::kChoice;
  bool selected = false;
  std::vector<MenuNode> children;
  friend bool operator==(const MenuNode&, const MenuNode&) = default;
};

struct UiConfig {
  std::int64_t auto_hide_ms = 3000;
  std::int64_t seek_step_ms = 5000;
  double volume_step = 0.05;
  std::int64_t spinner_delay_ms = 500;
  Layout layout = Layout::kDesktop;
  bool rtl = false;
  std::vector<std::string> hidden_controls;
  std::vector<std::string> control_order;
};

}

#endif
