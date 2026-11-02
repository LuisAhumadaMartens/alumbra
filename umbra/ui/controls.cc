#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {
namespace {

struct Spec {
  ControlId id;
  std::string_view name;
  ControlKind kind;
  Slot slot;
};

constexpr std::array<Spec, 7> kBuiltIn{{
    {.id = ControlId::kSeekBar,
     .name = "seek_bar",
     .kind = ControlKind::kSlider,
     .slot = Slot::kProgress},
    {.id = ControlId::kPlayPause,
     .name = "play_pause",
     .kind = ControlKind::kButton,
     .slot = Slot::kLeading},
    {.id = ControlId::kMute,
     .name = "mute",
     .kind = ControlKind::kToggleButton,
     .slot = Slot::kLeading},
    {.id = ControlId::kVolume,
     .name = "volume",
     .kind = ControlKind::kSlider,
     .slot = Slot::kLeading},
    {.id = ControlId::kSettings,
     .name = "settings",
     .kind = ControlKind::kMenuButton,
     .slot = Slot::kTrailing},
    {.id = ControlId::kFullscreen,
     .name = "fullscreen",
     .kind = ControlKind::kToggleButton,
     .slot = Slot::kTrailing},
    {.id = ControlId::kSkipAd,
     .name = "skip_ad",
     .kind = ControlKind::kButton,
     .slot = Slot::kTrailing},
}};

std::int64_t Seconds(Micros micros) { return micros / 1'000'000; }

TextArg IntArg(std::string name, std::int64_t value) {
  return TextArg{.name = std::move(name), .value = value};
}

Micros SeekSample(const UiState& state) {
  const Media& media = state.media;
  const auto* scrubbing = std::get_if<scrub::Scrubbing>(&state.scrub);
  if (scrubbing == nullptr) {
    return ClampToWindow(media, media.value_position_us);
  }
  const auto length =
      static_cast<double>(media.window_end_us - media.window_start_us);
  return media.window_start_us +
         static_cast<Micros>(std::llround(scrubbing->fraction * length));
}

void FillSeekBar(const UiState& state, ControlView& view) {
  const Media& media = state.media;
  const Micros sample = SeekSample(state);
  const Micros length = media.window_end_us - media.window_start_us;
  view.label_key = "control.seek";
  view.icon_token = "icon.seek";
  view.visible = !InAdBreak(state);
  view.enabled = CanSeek(state);
  view.value_min = 0;
  view.value_max = 1;
  view.value_now = length > 0
                       ? static_cast<double>(sample - media.window_start_us) /
                             static_cast<double>(length)
                       : 0;
  if (media.live) {
    view.value_text = ValueText{
        .key = "value.live",
        .args = {IntArg("behind_s", Seconds(media.window_end_us - sample))}};
  } else if (media.duration_known) {
    view.value_text =
        ValueText{.key = "value.time_of_total",
                  .args = {IntArg("elapsed_s", Seconds(sample)),
                           IntArg("total_s", Seconds(media.duration_us))}};
  }
}

void FillVolume(const UiState& state, ControlView& view) {
  const Media& media = state.media;
  view.label_key = "control.volume";
  view.icon_token = "icon.volume";
  view.visible = true;
  view.enabled = true;
  view.value_min = 0;
  view.value_max = 1;
  view.value_now = media.muted ? 0 : media.volume;
  if (media.muted) {
    view.value_text = ValueText{.key = "value.muted", .args = {}};
  } else {
    view.value_text = ValueText{
        .key = "value.percent",
        .args = {IntArg("percent", std::llround(media.volume * 100))}};
  }
}

void FillBuiltIn(const UiState& state, ControlView& view) {
  const Media& media = state.media;
  const bool in_ad = InAdBreak(state);
  switch (view.id) {
    case ControlId::kPlayPause:
      view.visible = true;
      view.enabled = PlaybackAvailable(state);
      view.label_key = media.paused ? "control.play" : "control.pause";
      view.icon_token = media.paused ? "icon.play" : "icon.pause";
      break;
    case ControlId::kSeekBar:
      FillSeekBar(state, view);
      break;
    case ControlId::kMute:
      view.visible = true;
      view.enabled = true;
      view.pressed = media.muted;
      view.label_key = "control.mute";
      view.icon_token = media.muted ? "icon.volume_off" : "icon.volume_on";
      break;
    case ControlId::kVolume:
      FillVolume(state, view);
      break;
    case ControlId::kSettings: {
      const bool has_items =
          state.menu_root != nullptr && !state.menu_root->children.empty();
      view.visible = !in_ad && has_items;
      view.enabled = true;
      view.expanded = MenuIsOpen(state);
      view.label_key = "control.settings";
      view.icon_token = "icon.settings";
      break;
    }
    case ControlId::kFullscreen: {
      const bool fullscreen = Is<presentation::Fullscreen>(state.presentation);
      view.visible = true;
      view.enabled = true;
      view.pressed = fullscreen;
      view.label_key = "control.fullscreen";
      view.icon_token =
          fullscreen ? "icon.fullscreen_exit" : "icon.fullscreen_enter";
      break;
    }
    case ControlId::kSkipAd: {
      const auto* adbreak = std::get_if<ad::Break>(&state.ad);
      view.visible =
          adbreak != nullptr && adbreak->skip != AdSkip::kNotSkippable;
      view.enabled = adbreak != nullptr && adbreak->skip == AdSkip::kAvailable;
      view.label_key = "control.skip_ad";
      view.icon_token = "icon.skip_ad";
      break;
    }
    case ControlId::kFirstCustom:
      break;
  }
}

ControlView BuiltInView(const UiState& state, const Spec& spec) {
  ControlView view;
  view.id = spec.id;
  view.name = std::string(spec.name);
  view.kind = spec.kind;
  view.role = spec.kind == ControlKind::kSlider ? Role::kSlider : Role::kButton;
  view.slot = spec.slot;
  FillBuiltIn(state, view);
  return view;
}

ControlView CustomView(const UiState& state, const CustomControl& custom) {
  ControlView view;
  view.id = custom.id;
  view.name = custom.name;
  view.kind = ControlKind::kCustom;
  view.role = Role::kButton;
  view.slot = custom.slot;
  view.visible = custom.visible && !InAdBreak(state);
  view.enabled = custom.enabled;
  view.pressed = custom.pressed;
  view.label_key = custom.label_key;
  view.icon_token = custom.icon_token;
  return view;
}

bool Contains(const std::vector<std::string>& names, std::string_view name) {
  return std::ranges::find(names, name) != names.end();
}

std::size_t OrderIndex(const UiConfig& config, const std::string& name) {
  const auto found = std::ranges::find(config.control_order, name);
  return static_cast<std::size_t>(found - config.control_order.begin());
}

}

const ControlView* FindControl(const std::vector<ControlView>& controls,
                               ControlId id) {
  const auto found = std::ranges::find_if(
      controls, [id](const ControlView& view) { return view.id == id; });
  return found == controls.end() ? nullptr : &*found;
}

std::vector<ControlView> DeriveControls(const UiState& state,
                                        const UiConfig& config) {
  std::vector<ControlView> views;
  for (const Spec& spec : kBuiltIn) {
    if (!Contains(config.hidden_controls, spec.name)) {
      views.push_back(BuiltInView(state, spec));
    }
  }
  for (const CustomControl& custom : state.custom) {
    if (!Contains(config.hidden_controls, custom.name)) {
      views.push_back(CustomView(state, custom));
    }
  }
  std::ranges::stable_sort(
      views, [&config](const ControlView& lhs, const ControlView& rhs) {
        if (lhs.slot != rhs.slot) {
          return lhs.slot < rhs.slot;
        }
        return OrderIndex(config, lhs.name) < OrderIndex(config, rhs.name);
      });
  const bool chrome_visible = ChromeVisible(state);
  const bool provider = ProviderOwnsInput(state);
  for (ControlView& view : views) {
    view.visible = view.visible && chrome_visible;
    view.enabled = view.enabled && !provider;
    view.focusable = view.visible && view.enabled;
  }
  return views;
}

std::vector<FocusEdges> BuildFocusEdges(
    const std::vector<ControlView>& controls) {
  std::vector<ControlId> row;
  std::optional<ControlId> seek;
  for (const ControlView& view : controls) {
    if (!view.focusable) {
      continue;
    }
    if (view.id == ControlId::kSeekBar) {
      seek = view.id;
    } else {
      row.push_back(view.id);
    }
  }
  std::vector<FocusEdges> edges;
  if (seek) {
    FocusEdges edge;
    edge.id = *seek;
    if (!row.empty()) {
      edge.down = row.front();
    }
    edges.push_back(edge);
  }
  for (std::size_t i = 0; i < row.size(); ++i) {
    FocusEdges edge;
    edge.id = row[i];
    if (i > 0) {
      edge.left = row[i - 1];
    }
    if (i + 1 < row.size()) {
      edge.right = row[i + 1];
    }
    if (row[i] != ControlId::kVolume) {
      edge.up = seek;
    }
    edges.push_back(edge);
  }
  return edges;
}

std::optional<ControlId> NearestFocusable(
    const std::vector<ControlView>& controls, ControlId from) {
  const auto origin = std::ranges::find_if(
      controls, [from](const ControlView& view) { return view.id == from; });
  const auto size = static_cast<std::ptrdiff_t>(controls.size());
  const std::ptrdiff_t center =
      origin == controls.end() ? 0 : origin - controls.begin();
  for (std::ptrdiff_t distance = 0; distance < size; ++distance) {
    for (const std::ptrdiff_t index : {center + distance, center - distance}) {
      if (index >= 0 && index < size &&
          controls[static_cast<std::size_t>(index)].focusable) {
        return controls[static_cast<std::size_t>(index)].id;
      }
    }
  }
  return std::nullopt;
}

}
