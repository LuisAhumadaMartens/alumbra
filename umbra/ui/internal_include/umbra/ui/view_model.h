#ifndef UMBRA_UI_VIEW_MODEL_H_
#define UMBRA_UI_VIEW_MODEL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "umbra/base/error_catalogue.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"

namespace umbra::ui {

struct ValueText {
  std::string key;
  std::vector<TextArg> args;
  friend bool operator==(const ValueText&, const ValueText&) = default;
};

struct ControlView {
  ControlId id = ControlId::kPlayPause;
  std::string name;
  ControlKind kind = ControlKind::kButton;
  Role role = Role::kButton;
  Slot slot = Slot::kLeading;
  bool visible = false;
  bool enabled = false;
  bool focusable = false;
  std::optional<bool> pressed;
  std::optional<bool> expanded;
  std::string label_key;
  std::string icon_token;
  std::optional<ValueText> value_text;
  double value_min = 0;
  double value_max = 0;
  double value_now = 0;
  friend bool operator==(const ControlView&, const ControlView&) = default;
};

struct FocusEdges {
  ControlId id = ControlId::kPlayPause;
  std::optional<ControlId> up;
  std::optional<ControlId> down;
  std::optional<ControlId> left;
  std::optional<ControlId> right;
  friend bool operator==(const FocusEdges&, const FocusEdges&) = default;
};

struct ControlsSection {
  std::uint64_t version = 0;
  std::vector<ControlView> controls;
  friend bool operator==(const ControlsSection&,
                         const ControlsSection&) = default;
};

struct MenuSection {
  std::uint64_t version = 0;
  bool open = false;
  std::vector<std::uint32_t> path;
  std::uint32_t highlighted = 0;
  std::shared_ptr<const MenuNode> root;
  friend bool operator==(const MenuSection&, const MenuSection&) = default;
};

struct ScrubPreview {
  double fraction = 0;
  Micros position_us = 0;
  friend bool operator==(const ScrubPreview&, const ScrubPreview&) = default;
};

struct SeekBarSection {
  std::uint64_t version = 0;
  Playhead playhead;
  Micros duration_us = 0;
  bool duration_known = false;
  bool live = false;
  Micros window_start_us = 0;
  Micros window_end_us = 0;
  std::vector<TimeRange> buffered;
  std::optional<ScrubPreview> scrub;
  friend bool operator==(const SeekBarSection&,
                         const SeekBarSection&) = default;
};

struct FocusSection {
  std::uint64_t version = 0;
  std::optional<ControlId> target;
  ControlId restore = ControlId::kPlayPause;
  std::vector<FocusEdges> edges;
  friend bool operator==(const FocusSection&, const FocusSection&) = default;
};

struct AnnouncementsSection {
  std::uint64_t version = 0;
  std::vector<Announcement> queue;
  friend bool operator==(const AnnouncementsSection&,
                         const AnnouncementsSection&) = default;
};

enum class PlaybackPhase {
  kIdle,
  kLoading,
  kPlaying,
  kPaused,
  kBuffering,
  kEnded,
  kError
};

struct ErrorPanel {
  base::ErrorCode code = base::ErrorCode::kPlayerInternal;
  bool retryable = false;
  friend bool operator==(const ErrorPanel&, const ErrorPanel&) = default;
};

struct ModesSection {
  std::uint64_t version = 0;
  PlaybackPhase phase = PlaybackPhase::kIdle;
  bool spinner = false;
  std::optional<ErrorPanel> error;
  bool chrome_visible = false;
  bool ad_active = false;
  AdInputOwner ad_owner = AdInputOwner::kUi;
  AdSkip ad_skip = AdSkip::kNotSkippable;
  PresentationMode presentation = PresentationMode::kInline;
  Layout layout = Layout::kDesktop;
  bool rtl = false;
  friend bool operator==(const ModesSection&, const ModesSection&) = default;
};

struct ViewModel {
  ControlsSection controls;
  MenuSection menu;
  SeekBarSection seek_bar;
  FocusSection focus;
  AnnouncementsSection announcements;
  ModesSection modes;
};

[[nodiscard]] std::vector<ControlView> DeriveControls(const UiState& state,
                                                      const UiConfig& config);
[[nodiscard]] std::vector<FocusEdges> BuildFocusEdges(
    const std::vector<ControlView>& controls);
[[nodiscard]] std::optional<ControlId> NearestFocusable(
    const std::vector<ControlView>& controls, ControlId from);
[[nodiscard]] const ControlView* FindControl(
    const std::vector<ControlView>& controls, ControlId id);
[[nodiscard]] bool VersionsDiffer(const ViewModel& lhs, const ViewModel& rhs);
[[nodiscard]] ViewModel BuildViewModel(const UiState& state,
                                       const UiConfig& config,
                                       const ViewModel& previous);

}

#endif
