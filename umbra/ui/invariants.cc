#include <string>
#include <variant>
#include <vector>

#include "umbra/ui/reduce.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {
namespace {

void CheckModes(const UiState& state, std::vector<std::string>& violations) {
  if (InAdBreak(state) && MenuIsOpen(state)) {
    violations.emplace_back("a menu is open during an ad break");
  }
  if (InAdBreak(state) && Is<scrub::Scrubbing>(state.scrub)) {
    violations.emplace_back("a scrub is active during an ad break");
  }
  if (MenuIsOpen(state) && !ChromeVisible(state)) {
    violations.emplace_back("the chrome is hidden while a menu is open");
  }
  if (const auto* open = std::get_if<menu::Open>(&state.menu)) {
    const std::vector<MenuNode>* items = MenuItems(state, open->path);
    if (items == nullptr || open->highlighted >= items->size()) {
      violations.emplace_back("the open menu path or highlight is invalid");
    }
  }
  if (const auto* pinned = std::get_if<chrome::Pinned>(&state.chrome)) {
    if (pinned->menu != MenuIsOpen(state) ||
        pinned->scrub != Is<scrub::Scrubbing>(state.scrub)) {
      violations.emplace_back("the chrome pins disagree with the regions");
    }
    if (!pinned->menu && !pinned->pointer && !pinned->scrub) {
      violations.emplace_back("the chrome is pinned for no reason");
    }
  }
  if (state.media.playhead.rate != EffectiveRate(state)) {
    violations.emplace_back("the playhead rate is not the effective rate");
  }
}

void CheckControls(const UiState& state, const UiConfig& config,
                   std::vector<std::string>& violations) {
  const std::vector<ControlView> controls = DeriveControls(state, config);
  if (const auto* on = std::get_if<focus::On>(&state.focus)) {
    const ControlView* target = FindControl(controls, on->target);
    if (target == nullptr || !target->visible || !target->enabled ||
        !target->focusable) {
      violations.emplace_back(
          "the focus target is not a visible, enabled, focusable control");
    }
  }
  for (const FocusEdges& edges : BuildFocusEdges(controls)) {
    for (const auto& edge : {edges.up, edges.down, edges.left, edges.right}) {
      if (edge && FindControl(controls, *edge) == nullptr) {
        violations.emplace_back("a focus edge names a missing control");
      }
    }
  }
  if (InAdBreak(state)) {
    for (const ControlView& control : controls) {
      const bool hidden_by_ads = control.id == ControlId::kSeekBar ||
                                 control.id == ControlId::kSettings ||
                                 control.kind == ControlKind::kCustom;
      if (hidden_by_ads && (control.visible || control.focusable)) {
        violations.emplace_back("ad mode shows a control it hides");
      }
    }
  }
}

}

std::vector<std::string> CheckInvariants(const UiState& state,
                                         const UiConfig& config) {
  std::vector<std::string> violations;
  CheckModes(state, violations);
  CheckControls(state, config, violations);
  return violations;
}

}
