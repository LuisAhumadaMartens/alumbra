#include "umbra/ui/view_model.h"

#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "umbra/base/error_catalogue.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"

namespace umbra::ui {
namespace {

PlaybackPhase PhaseOf(const PlaybackRegion& region) {
  return static_cast<PlaybackPhase>(region.index());
}

bool SpinnerVisible(const PlaybackRegion& region) {
  if (const auto* loading = std::get_if<playback::Loading>(&region)) {
    return loading->spinner;
  }
  if (const auto* buffering = std::get_if<playback::Buffering>(&region)) {
    return buffering->spinner;
  }
  return false;
}

std::optional<ErrorPanel> ErrorOf(const PlaybackRegion& region) {
  const auto* error = std::get_if<playback::Error>(&region);
  if (error == nullptr) {
    return std::nullopt;
  }
  const base::ErrorCatalogueEntry* entry = base::FindError(error->code);
  return ErrorPanel{.code = error->code,
                    .retryable = entry != nullptr && entry->retryable};
}

MenuSection MenuOf(const UiState& state) {
  MenuSection section;
  section.root = state.menu_root;
  if (const auto* open = std::get_if<menu::Open>(&state.menu)) {
    section.open = true;
    section.path = open->path;
    section.highlighted = open->highlighted;
  }
  return section;
}

SeekBarSection SeekBarOf(const UiState& state) {
  const Media& media = state.media;
  SeekBarSection section;
  section.playhead = media.playhead;
  section.duration_us = media.duration_us;
  section.duration_known = media.duration_known;
  section.live = media.live;
  section.window_start_us = media.window_start_us;
  section.window_end_us = media.window_end_us;
  section.buffered = media.buffered;
  if (const auto* scrubbing = std::get_if<scrub::Scrubbing>(&state.scrub)) {
    const auto length =
        static_cast<double>(media.window_end_us - media.window_start_us);
    section.scrub = ScrubPreview{
        .fraction = scrubbing->fraction,
        .position_us =
            media.window_start_us +
            static_cast<Micros>(std::llround(scrubbing->fraction * length))};
  }
  return section;
}

FocusSection FocusOf(const UiState& state,
                     const std::vector<ControlView>& controls) {
  FocusSection section;
  if (const auto* on = std::get_if<focus::On>(&state.focus)) {
    section.target = on->target;
    section.restore = on->restore;
  } else {
    section.restore = std::get<focus::None>(state.focus).restore;
  }
  section.edges = BuildFocusEdges(controls);
  return section;
}

ModesSection ModesOf(const UiState& state, const UiConfig& config) {
  ModesSection section;
  section.phase = PhaseOf(state.playback);
  section.spinner = SpinnerVisible(state.playback);
  section.error = ErrorOf(state.playback);
  section.chrome_visible = ChromeVisible(state);
  if (const auto* adbreak = std::get_if<ad::Break>(&state.ad)) {
    section.ad_active = true;
    section.ad_owner = adbreak->owner;
    section.ad_skip = adbreak->skip;
  }
  section.presentation =
      static_cast<PresentationMode>(state.presentation.index());
  section.layout = config.layout;
  section.rtl = config.rtl;
  return section;
}

template <class Section>
void Bump(Section& next, const Section& previous) {
  next.version = previous.version;
  if (!(next == previous)) {
    next.version = previous.version + 1;
  }
}

}

bool VersionsDiffer(const ViewModel& lhs, const ViewModel& rhs) {
  return lhs.controls.version != rhs.controls.version ||
         lhs.menu.version != rhs.menu.version ||
         lhs.seek_bar.version != rhs.seek_bar.version ||
         lhs.focus.version != rhs.focus.version ||
         lhs.announcements.version != rhs.announcements.version ||
         lhs.modes.version != rhs.modes.version;
}

ViewModel BuildViewModel(const UiState& state, const UiConfig& config,
                         const ViewModel& previous) {
  ViewModel next;
  next.controls.controls = DeriveControls(state, config);
  next.menu = MenuOf(state);
  next.seek_bar = SeekBarOf(state);
  next.focus = FocusOf(state, next.controls.controls);
  next.announcements.queue = state.announcements;
  next.modes = ModesOf(state, config);
  Bump(next.controls, previous.controls);
  Bump(next.menu, previous.menu);
  Bump(next.seek_bar, previous.seek_bar);
  Bump(next.focus, previous.focus);
  Bump(next.announcements, previous.announcements);
  Bump(next.modes, previous.modes);
  return next;
}

}
