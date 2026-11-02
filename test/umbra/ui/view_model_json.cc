#include "view_model_json.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "umbra/ui/effect.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {
namespace {

template <class... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

class Writer {
 public:
  void Begin(char open) {
    Separate();
    text_ += open;
    stack_.push_back(false);
  }
  void End(char close) {
    const bool had_items = stack_.back();
    stack_.pop_back();
    if (had_items) {
      Newline();
    }
    text_ += close;
  }
  void Key(std::string_view key) {
    Separate();
    text_ += '"';
    text_ += key;
    text_ += "\": ";
    after_key_ = true;
  }
  void String(std::string_view value) {
    Separate();
    text_ += '"';
    text_ += value;
    text_ += '"';
  }
  void Raw(std::string_view value) {
    Separate();
    text_ += value;
  }
  void Int(std::int64_t value) { Raw(std::to_string(value)); }
  void Bool(bool value) { Raw(value ? "true" : "false"); }
  void Null() { Raw("null"); }
  void Double(double value) {
    std::array<char, 32> buffer{};
    const auto result =
        std::to_chars(buffer.data(), std::to_address(buffer.end()), value);
    Raw(std::string_view(buffer.data(), static_cast<std::size_t>(std::distance(
                                            buffer.data(), result.ptr))));
  }
  std::string Take() { return std::move(text_) + "\n"; }

 private:
  void Separate() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (stack_.empty()) {
      return;
    }
    if (stack_.back()) {
      text_ += ',';
    }
    stack_.back() = true;
    Newline();
  }
  void Newline() {
    text_ += '\n';
    text_.append(stack_.size() * 2, ' ');
  }

  std::string text_;
  std::vector<bool> stack_;
  bool after_key_ = false;
};

template <class E, std::size_t N>
std::string_view Name(const std::array<std::string_view, N>& names, E value) {
  return names.at(static_cast<std::size_t>(value));
}

constexpr std::array<std::string_view, 5> kControlKinds{
    "button", "toggle_button", "slider", "menu_button", "custom"};
constexpr std::array<std::string_view, 6> kRoles{
    "button",   "slider",        "menu",
    "menuitem", "menuitemradio", "menuitemcheckbox"};
constexpr std::array<std::string_view, 3> kSlots{"progress", "leading",
                                                 "trailing"};
constexpr std::array<std::string_view, 3> kLayouts{"desktop", "mobile", "tv"};
constexpr std::array<std::string_view, 2> kPoliteness{"polite", "assertive"};
constexpr std::array<std::string_view, 2> kOwners{"ui", "provider"};
constexpr std::array<std::string_view, 3> kSkips{"not_skippable", "waiting",
                                                 "available"};
constexpr std::array<std::string_view, 3> kPresentations{"inline", "fullscreen",
                                                         "picture_in_picture"};
constexpr std::array<std::string_view, 7> kPhases{
    "idle", "loading", "playing", "paused", "buffering", "ended", "error"};
constexpr std::array<std::string_view, 3> kTimers{"auto_hide", "spinner",
                                                  "value_text"};
constexpr std::array<std::string_view, 3> kMenuKinds{"submenu", "choice",
                                                     "toggle"};

std::string ControlName(const std::vector<ControlView>& controls,
                        ControlId id) {
  const ControlView* control = FindControl(controls, id);
  return control == nullptr ? "?" + std::to_string(static_cast<int>(id))
                            : control->name;
}

void WriteOptionalId(Writer& out, const std::vector<ControlView>& controls,
                     const std::optional<ControlId>& id) {
  if (id) {
    out.String(ControlName(controls, *id));
  } else {
    out.Null();
  }
}

void WriteArgs(Writer& out, const std::vector<TextArg>& args) {
  out.Begin('{');
  for (const TextArg& arg : args) {
    out.Key(arg.name);
    std::visit(
        Overloaded{[&out](std::int64_t value) { out.Int(value); },
                   [&out](double value) { out.Double(value); },
                   [&out](const std::string& value) { out.String(value); }},
        arg.value);
  }
  out.End('}');
}

void WriteControl(Writer& out, const ControlView& control) {
  out.Begin('{');
  out.Key("name");
  out.String(control.name);
  out.Key("kind");
  out.String(Name(kControlKinds, control.kind));
  out.Key("role");
  out.String(Name(kRoles, control.role));
  out.Key("slot");
  out.String(Name(kSlots, control.slot));
  out.Key("visible");
  out.Bool(control.visible);
  out.Key("enabled");
  out.Bool(control.enabled);
  out.Key("focusable");
  out.Bool(control.focusable);
  out.Key("pressed");
  if (control.pressed) {
    out.Bool(*control.pressed);
  } else {
    out.Null();
  }
  out.Key("expanded");
  if (control.expanded) {
    out.Bool(*control.expanded);
  } else {
    out.Null();
  }
  out.Key("label_key");
  out.String(control.label_key);
  out.Key("icon_token");
  out.String(control.icon_token);
  if (control.role == Role::kSlider) {
    out.Key("value_min");
    out.Double(control.value_min);
    out.Key("value_max");
    out.Double(control.value_max);
    out.Key("value_now");
    out.Double(control.value_now);
  }
  if (control.value_text) {
    out.Key("value_text");
    out.Begin('{');
    out.Key("key");
    out.String(control.value_text->key);
    out.Key("args");
    WriteArgs(out, control.value_text->args);
    out.End('}');
  }
  out.End('}');
}

void WriteMenuNode(Writer& out, const MenuNode& node) {
  out.Begin('{');
  out.Key("id");
  out.String(node.id);
  out.Key("label_key");
  out.String(node.label_key);
  out.Key("kind");
  out.String(Name(kMenuKinds, node.kind));
  out.Key("selected");
  out.Bool(node.selected);
  out.Key("children");
  out.Begin('[');
  for (const MenuNode& child : node.children) {
    WriteMenuNode(out, child);
  }
  out.End(']');
  out.End('}');
}

void WriteRange(Writer& out, const TimeRange& range) {
  out.Begin('[');
  out.Int(range.start_us);
  out.Int(range.end_us);
  out.End(']');
}

void WriteControlsSection(Writer& out, const ViewModel& vm) {
  out.Key("controls");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(vm.controls.version));
  out.Key("items");
  out.Begin('[');
  for (const ControlView& control : vm.controls.controls) {
    WriteControl(out, control);
  }
  out.End(']');
  out.End('}');
}

void WriteMenuSection(Writer& out, const ViewModel& vm) {
  out.Key("menu");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(vm.menu.version));
  out.Key("open");
  out.Bool(vm.menu.open);
  out.Key("path");
  out.Begin('[');
  for (const std::uint32_t index : vm.menu.path) {
    out.Int(index);
  }
  out.End(']');
  out.Key("highlighted");
  out.Int(vm.menu.highlighted);
  out.Key("root");
  if (vm.menu.root) {
    WriteMenuNode(out, *vm.menu.root);
  } else {
    out.Null();
  }
  out.End('}');
}

void WriteSeekBarSection(Writer& out, const ViewModel& vm) {
  const SeekBarSection& seek = vm.seek_bar;
  out.Key("seek_bar");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(seek.version));
  out.Key("playhead");
  out.Begin('{');
  out.Key("position_us");
  out.Int(seek.playhead.position_us);
  out.Key("rate");
  out.Double(seek.playhead.rate);
  out.Key("anchor_us");
  out.Int(seek.playhead.anchor_us);
  out.End('}');
  out.Key("duration_us");
  out.Int(seek.duration_us);
  out.Key("duration_known");
  out.Bool(seek.duration_known);
  out.Key("live");
  out.Bool(seek.live);
  out.Key("window_us");
  WriteRange(out, TimeRange{.start_us = seek.window_start_us,
                            .end_us = seek.window_end_us});
  out.Key("buffered_us");
  out.Begin('[');
  for (const TimeRange& range : seek.buffered) {
    WriteRange(out, range);
  }
  out.End(']');
  out.Key("scrub");
  if (seek.scrub) {
    out.Begin('{');
    out.Key("fraction");
    out.Double(seek.scrub->fraction);
    out.Key("position_us");
    out.Int(seek.scrub->position_us);
    out.End('}');
  } else {
    out.Null();
  }
  out.End('}');
}

void WriteFocusSection(Writer& out, const ViewModel& vm) {
  const auto& controls = vm.controls.controls;
  out.Key("focus");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(vm.focus.version));
  out.Key("target");
  WriteOptionalId(out, controls, vm.focus.target);
  out.Key("restore");
  out.String(ControlName(controls, vm.focus.restore));
  out.Key("edges");
  out.Begin('[');
  for (const FocusEdges& edges : vm.focus.edges) {
    out.Begin('{');
    out.Key("id");
    out.String(ControlName(controls, edges.id));
    out.Key("up");
    WriteOptionalId(out, controls, edges.up);
    out.Key("down");
    WriteOptionalId(out, controls, edges.down);
    out.Key("left");
    WriteOptionalId(out, controls, edges.left);
    out.Key("right");
    WriteOptionalId(out, controls, edges.right);
    out.End('}');
  }
  out.End(']');
  out.End('}');
}

void WriteAnnouncementsSection(Writer& out, const ViewModel& vm) {
  out.Key("announcements");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(vm.announcements.version));
  out.Key("queue");
  out.Begin('[');
  for (const Announcement& item : vm.announcements.queue) {
    out.Begin('{');
    out.Key("sequence");
    out.Int(static_cast<std::int64_t>(item.sequence));
    out.Key("key");
    out.String(item.key);
    out.Key("args");
    WriteArgs(out, item.args);
    out.Key("politeness");
    out.String(Name(kPoliteness, item.politeness));
    out.End('}');
  }
  out.End(']');
  out.End('}');
}

void WriteModesSection(Writer& out, const ViewModel& vm) {
  const ModesSection& modes = vm.modes;
  out.Key("modes");
  out.Begin('{');
  out.Key("version");
  out.Int(static_cast<std::int64_t>(modes.version));
  out.Key("phase");
  out.String(Name(kPhases, modes.phase));
  out.Key("spinner");
  out.Bool(modes.spinner);
  out.Key("error");
  if (modes.error) {
    out.Begin('{');
    out.Key("code");
    out.Int(static_cast<std::int64_t>(modes.error->code));
    out.Key("retryable");
    out.Bool(modes.error->retryable);
    out.End('}');
  } else {
    out.Null();
  }
  out.Key("chrome_visible");
  out.Bool(modes.chrome_visible);
  out.Key("ad");
  out.Begin('{');
  out.Key("active");
  out.Bool(modes.ad_active);
  out.Key("owner");
  out.String(Name(kOwners, modes.ad_owner));
  out.Key("skip");
  out.String(Name(kSkips, modes.ad_skip));
  out.End('}');
  out.Key("presentation");
  out.String(Name(kPresentations, modes.presentation));
  out.Key("layout");
  out.String(Name(kLayouts, modes.layout));
  out.Key("rtl");
  out.Bool(modes.rtl);
  out.End('}');
}

void WriteEffect(Writer& out, const SequencedEffect& sequenced) {
  out.Begin('{');
  out.Key("sequence");
  out.Int(static_cast<std::int64_t>(sequenced.sequence));
  std::visit(Overloaded{[&out](const PlayerPlay&) {
                          out.Key("effect");
                          out.String("player_play");
                        },
                        [&out](const PlayerPause&) {
                          out.Key("effect");
                          out.String("player_pause");
                        },
                        [&out](const PlayerSeek& seek) {
                          out.Key("effect");
                          out.String("player_seek");
                          out.Key("position_us");
                          out.Int(seek.position_us);
                          out.Key("precise");
                          out.Bool(seek.precise);
                        },
                        [&out](const PlayerSetVolume& volume) {
                          out.Key("effect");
                          out.String("player_set_volume");
                          out.Key("volume");
                          out.Double(volume.volume);
                        },
                        [&out](const PlayerSetMuted& muted) {
                          out.Key("effect");
                          out.String("player_set_muted");
                          out.Key("muted");
                          out.Bool(muted.muted);
                        },
                        [&out](const RequestPresentation& request) {
                          out.Key("effect");
                          out.String("request_presentation");
                          out.Key("mode");
                          out.String(Name(kPresentations, request.mode));
                        },
                        [&out](const StartTimer& timer) {
                          out.Key("effect");
                          out.String("start_timer");
                          out.Key("kind");
                          out.String(Name(kTimers, timer.kind));
                          out.Key("generation");
                          out.Int(static_cast<std::int64_t>(timer.generation));
                          out.Key("delay_us");
                          out.Int(timer.delay_us);
                        },
                        [&out](const BackToApp&) {
                          out.Key("effect");
                          out.String("back_to_app");
                        },
                        [&out](const CustomAction& action) {
                          out.Key("effect");
                          out.String("custom_action");
                          out.Key("name");
                          out.String(action.name);
                        },
                        [&out](const MenuChoice& choice) {
                          out.Key("effect");
                          out.String("menu_choice");
                          out.Key("item");
                          out.String(choice.item_id);
                        },
                        [&out](const SkipAdRequested&) {
                          out.Key("effect");
                          out.String("skip_ad");
                        }},
             sequenced.effect);
  out.End('}');
}

}

std::string ToJson(const ViewModel& view_model,
                   const std::vector<SequencedEffect>& effects) {
  Writer out;
  out.Begin('{');
  out.Key("effects");
  out.Begin('[');
  for (const SequencedEffect& effect : effects) {
    WriteEffect(out, effect);
  }
  out.End(']');
  out.Key("view_model");
  out.Begin('{');
  WriteControlsSection(out, view_model);
  WriteMenuSection(out, view_model);
  WriteSeekBarSection(out, view_model);
  WriteFocusSection(out, view_model);
  WriteAnnouncementsSection(out, view_model);
  WriteModesSection(out, view_model);
  out.End('}');
  out.End('}');
  return out.Take();
}

}
