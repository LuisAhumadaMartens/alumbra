#include "umbra/ui/reduce.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
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

template <class T, class V>
struct InVariant : std::false_type {};
template <class T, class... Ts>
struct InVariant<T, std::variant<Ts...>>
    : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

template <class T>
const T* As(const Input& input) {
  if constexpr (InVariant<T, Intent>::value) {
    const auto* intent = std::get_if<Intent>(&input);
    return intent == nullptr ? nullptr : std::get_if<T>(intent);
  } else if constexpr (InVariant<T, EngineEvent>::value) {
    const auto* event = std::get_if<EngineEvent>(&input);
    return event == nullptr ? nullptr : std::get_if<T>(event);
  } else if constexpr (InVariant<T, CatalogChange>::value) {
    const auto* change = std::get_if<CatalogChange>(&input);
    return change == nullptr ? nullptr : std::get_if<T>(change);
  } else {
    return std::get_if<T>(&input);
  }
}

struct Fx {
  std::vector<Effect> effects;
  std::uint64_t next_timer = 1;

  template <class E>
  void Emit(E&& effect) {
    effects.emplace_back(std::forward<E>(effect));
  }

  std::uint64_t Arm(TimerKind kind, Micros delay_us) {
    const std::uint64_t generation = next_timer++;
    effects.emplace_back(StartTimer{
        .kind = kind, .generation = generation, .delay_us = delay_us});
    return generation;
  }
};

struct View {
  const UiState* prev = nullptr;
  const UiState* next = nullptr;
  const UiConfig* config = nullptr;
  const std::vector<ControlView>* controls = nullptr;
  Micros now_us = 0;
};

Micros MillisToMicros(std::int64_t millis) { return millis * 1000; }

Micros FractionToPosition(const Media& media, double fraction) {
  const auto length =
      static_cast<double>(media.window_end_us - media.window_start_us);
  return media.window_start_us + static_cast<Micros>(std::llround(
                                     std::clamp(fraction, 0.0, 1.0) * length));
}

bool IsActivity(const Input& input) {
  const auto* intent = std::get_if<Intent>(&input);
  if (intent == nullptr) {
    return false;
  }
  return !(std::holds_alternative<HideControls>(*intent) ||
           std::holds_alternative<PointerLeft>(*intent) ||
           std::holds_alternative<PassBack>(*intent));
}

bool Forward(const UiConfig& config, Direction direction) {
  return direction == (config.rtl ? Direction::kLeft : Direction::kRight);
}

std::optional<Input> ActivateControl(const ControlView& control) {
  switch (control.id) {
    case ControlId::kPlayPause:
      return Input{Intent{TogglePlay{}}};
    case ControlId::kMute:
      return Input{Intent{ToggleMute{}}};
    case ControlId::kFullscreen:
      return Input{Intent{ToggleFullscreen{}}};
    case ControlId::kSettings:
      return Input{Intent{OpenMenu{}}};
    case ControlId::kSkipAd:
      return Input{Intent{SkipAd{}}};
    case ControlId::kSeekBar:
    case ControlId::kVolume:
      return std::nullopt;
    case ControlId::kFirstCustom:
      break;
  }
  return Input{Intent{ActivateCustom{.id = control.id}}};
}

std::optional<Input> ResolveActivate(const View& view, ControlId id) {
  const ControlView* control = FindControl(*view.controls, id);
  if (control == nullptr || !control->visible || !control->enabled) {
    return std::nullopt;
  }
  return ActivateControl(*control);
}

std::optional<Input> ResolveFocused(ControlId target, Direction direction) {
  const bool horizontal =
      direction == Direction::kLeft || direction == Direction::kRight;
  if (target == ControlId::kSeekBar && horizontal) {
    return Input{
        Intent{SeekSteps{.steps = direction == Direction::kRight ? 1 : -1}}};
  }
  if (target == ControlId::kVolume && !horizontal) {
    return Input{
        Intent{VolumeSteps{.steps = direction == Direction::kUp ? 1 : -1}}};
  }
  return Input{Intent{MoveFocus{.direction = direction}}};
}

std::optional<Input> ResolveNavigate(const View& view, Direction direction) {
  const UiState& prev = *view.prev;
  if (MenuIsOpen(prev)) {
    return Input{Intent{MenuNavigate{.direction = direction}}};
  }
  if (const auto* on = std::get_if<focus::On>(&prev.focus)) {
    return ResolveFocused(on->target, direction);
  }
  if (view.config->layout == Layout::kTv) {
    return Input{Intent{FocusChrome{}}};
  }
  if (direction == Direction::kLeft || direction == Direction::kRight) {
    if (!CanSeek(prev)) {
      return std::nullopt;
    }
    return Input{
        Intent{SeekSteps{.steps = direction == Direction::kRight ? 1 : -1}}};
  }
  return Input{
      Intent{VolumeSteps{.steps = direction == Direction::kUp ? 1 : -1}}};
}

bool AllowedWhileProviderOwnsInput(const Intent& intent) {
  return std::holds_alternative<PointerActivity>(intent) ||
         std::holds_alternative<PointerEntered>(intent) ||
         std::holds_alternative<PointerLeft>(intent) ||
         std::holds_alternative<ShowControls>(intent) ||
         std::holds_alternative<HideControls>(intent) ||
         std::holds_alternative<ToggleControls>(intent) ||
         std::holds_alternative<Back>(intent) ||
         std::holds_alternative<PassBack>(intent);
}

std::optional<Input> NormalizeIntent(const View& view, const Intent& intent) {
  const UiState& prev = *view.prev;
  const Input same{intent};
  const auto allow = [&same](bool allowed) {
    return allowed ? std::optional<Input>(same) : std::nullopt;
  };
  const auto control_enabled = [&view](ControlId id) {
    const ControlView* control = FindControl(*view.controls, id);
    return control != nullptr && control->enabled;
  };
  return std::visit(
      Overloaded{
          [&](const TogglePlay&) { return allow(PlaybackAvailable(prev)); },
          [&](const Play&) { return allow(PlaybackAvailable(prev)); },
          [&](const Pause&) { return allow(PlaybackAvailable(prev)); },
          [&](const SeekSteps&) {
            return allow(CanSeek(prev) && !Is<scrub::Scrubbing>(prev.scrub));
          },
          [&](const SeekToFraction&) {
            return allow(CanSeek(prev) && !Is<scrub::Scrubbing>(prev.scrub));
          },
          [&](const ScrubBegin&) { return allow(CanSeek(prev)); },
          [&](const ToggleControls&) {
            return std::optional<Input>(ChromeVisible(prev)
                                            ? Input{Intent{HideControls{}}}
                                            : Input{Intent{ShowControls{}}});
          },
          [&](const Navigate& navigate) {
            return ResolveNavigate(view, navigate.direction);
          },
          [&](const Back&) {
            if (MenuIsOpen(prev)) {
              return std::optional<Input>(Input{Intent{MenuBack{}}});
            }
            if (ChromeVisible(prev)) {
              return std::optional<Input>(Input{Intent{HideControls{}}});
            }
            return std::optional<Input>(Input{Intent{PassBack{}}});
          },
          [&](const ActivateFocused&) {
            if (MenuIsOpen(prev)) {
              return std::optional<Input>(Input{Intent{MenuActivate{}}});
            }
            if (const auto* on = std::get_if<focus::On>(&prev.focus)) {
              return ResolveActivate(view, on->target);
            }
            if (view.config->layout == Layout::kTv) {
              return std::optional<Input>(Input{Intent{FocusChrome{}}});
            }
            return std::optional<Input>();
          },
          [&](const Activate& activate) {
            return ResolveActivate(view, activate.id);
          },
          [&](const FocusControl& focus_control) {
            const ControlView* control =
                FindControl(*view.controls, focus_control.id);
            return allow(control != nullptr && control->focusable);
          },
          [&](const FocusTraverse&) { return allow(!MenuIsOpen(prev)); },
          [&](const OpenMenu&) {
            return allow(control_enabled(ControlId::kSettings));
          },
          [&](const MenuNavigate&) { return allow(MenuIsOpen(prev)); },
          [&](const MenuBack&) { return allow(MenuIsOpen(prev)); },
          [&](const MenuActivate&) { return allow(MenuIsOpen(prev)); },
          [&](const SkipAd&) {
            return allow(control_enabled(ControlId::kSkipAd));
          },
          [&](const ActivateCustom& custom) {
            const ControlView* control = FindControl(*view.controls, custom.id);
            return allow(control != nullptr && control->enabled &&
                         control->visible &&
                         control->kind == ControlKind::kCustom);
          },
          [&](const auto&) { return std::optional<Input>(same); }},
      intent);
}

std::optional<Input> Normalize(const View& view, const Input& input) {
  const auto* intent = std::get_if<Intent>(&input);
  if (intent == nullptr) {
    return input;
  }
  if (ProviderOwnsInput(*view.prev) &&
      !AllowedWhileProviderOwnsInput(*intent)) {
    return std::nullopt;
  }
  return NormalizeIntent(view, *intent);
}

void ReduceCatalog(UiState& next, const Input& input) {
  if (const auto* add = As<RegisterControl>(input)) {
    const auto exists =
        std::ranges::any_of(next.custom, [add](const CustomControl& custom) {
          return custom.name == add->name;
        });
    if (add->name.empty() || exists) {
      return;
    }
    CustomControl custom;
    custom.id = static_cast<ControlId>(
        static_cast<std::uint16_t>(ControlId::kFirstCustom) +
        next.custom.size());
    custom.name = add->name;
    custom.label_key = add->label_key;
    custom.icon_token = add->icon_token;
    custom.slot = add->slot;
    next.custom.push_back(std::move(custom));
  } else if (const auto* update = As<UpdateControl>(input)) {
    for (CustomControl& custom : next.custom) {
      if (custom.name == update->name) {
        custom.visible = update->visible.value_or(custom.visible);
        custom.enabled = update->enabled.value_or(custom.enabled);
        custom.pressed = update->pressed ? update->pressed : custom.pressed;
      }
    }
  } else if (const auto* menu_change = As<SetMenu>(input)) {
    next.menu_root = menu_change->root;
  }
}

void JumpTo(Media& media, Micros position_us, Micros now_us) {
  const Micros clamped = ClampToWindow(media, position_us);
  media.playhead = Playhead{
      .position_us = clamped, .rate = media.playhead.rate, .anchor_us = now_us};
  media.value_position_us = clamped;
}

void ApplyVolume(Media& media, double volume, Fx& fx) {
  const double clamped = std::round(std::clamp(volume, 0.0, 1.0) * 1000) / 1000;
  if (clamped != media.volume) {
    media.volume = clamped;
    fx.Emit(PlayerSetVolume{.volume = clamped});
  }
  if (clamped > 0 && media.muted) {
    media.muted = false;
    fx.Emit(PlayerSetMuted{.muted = false});
  }
}

void SeekTo(Media& media, Micros target_us, Micros now_us, Fx& fx) {
  JumpTo(media, target_us, now_us);
  media.seeking = true;
  fx.Emit(
      PlayerSeek{.position_us = media.playhead.position_us, .precise = true});
}

void ReducePlayIntent(Media& media, const View& view, const Input& input,
                      Fx& fx) {
  std::optional<bool> paused;
  if (As<TogglePlay>(input) != nullptr) {
    paused = !media.paused;
  } else if (As<Play>(input) != nullptr) {
    paused = false;
  } else if (As<Pause>(input) != nullptr) {
    paused = true;
  }
  if (!paused || *paused == media.paused) {
    return;
  }
  media.paused = *paused;
  if (*paused) {
    fx.Emit(PlayerPause{});
  } else {
    fx.Emit(PlayerPlay{});
  }
  if (!*paused && Is<playback::Ended>(view.prev->playback)) {
    JumpTo(media, media.window_start_us, view.now_us);
  }
}

void ReduceMediaIntent(Media& media, const View& view, const Input& input,
                       Fx& fx) {
  const Micros now = view.now_us;
  const UiConfig& config = *view.config;
  ReducePlayIntent(media, view, input, fx);
  if (const auto* steps = As<SeekSteps>(input)) {
    const Micros current = ExtrapolatePlayhead(media, now);
    SeekTo(media,
           current + (steps->steps * MillisToMicros(config.seek_step_ms)), now,
           fx);
  } else if (const auto* fraction = As<SeekToFraction>(input)) {
    SeekTo(media, FractionToPosition(media, fraction->fraction), now, fx);
  } else if (As<ScrubEnd>(input) != nullptr) {
    if (const auto* scrubbing =
            std::get_if<scrub::Scrubbing>(&view.prev->scrub)) {
      SeekTo(media, FractionToPosition(media, scrubbing->fraction), now, fx);
    }
  } else if (const auto* volume = As<SetVolume>(input)) {
    ApplyVolume(media, volume->volume, fx);
  } else if (const auto* volume_steps = As<VolumeSteps>(input)) {
    ApplyVolume(media,
                media.volume + (volume_steps->steps * config.volume_step), fx);
  } else if (As<ToggleMute>(input) != nullptr) {
    media.muted = !media.muted;
    fx.Emit(PlayerSetMuted{.muted = media.muted});
  }
}

Media ResetMedia(const Media& previous, Micros now_us) {
  Media media;
  media.paused = previous.paused;
  media.volume = previous.volume;
  media.muted = previous.muted;
  media.rate = previous.rate;
  media.resample_ratio = previous.resample_ratio;
  media.playhead.anchor_us = now_us;
  return media;
}

void ReduceTimeUpdate(Media& media, const View& view,
                      const TimeUpdate& update) {
  if (media.seeking || Is<scrub::Scrubbing>(view.prev->scrub)) {
    return;
  }
  const Micros at = update.anchor_us > 0 ? update.anchor_us : view.now_us;
  const Micros expected = ExtrapolatePlayhead(media, at);
  const Micros drift = update.position_us > expected
                           ? update.position_us - expected
                           : expected - update.position_us;
  if (drift > kPlayheadToleranceUs) {
    const Micros position = ClampToWindow(media, update.position_us);
    media.playhead = Playhead{
        .position_us = position, .rate = media.playhead.rate, .anchor_us = at};
    media.value_position_us = position;
  }
}

void ReduceDuration(Media& media, const DurationChanged& changed) {
  media.duration_us = std::max<Micros>(changed.duration_us, 0);
  media.duration_known = true;
  media.live = changed.live;
  if (!changed.live) {
    media.window_start_us = 0;
    media.window_end_us = media.duration_us;
  }
  media.playhead.position_us = ClampToWindow(media, media.playhead.position_us);
  media.value_position_us = ClampToWindow(media, media.value_position_us);
}

void ReduceMediaEvent(Media& media, const View& view, const Input& input) {
  if (As<LoadStarted>(input) != nullptr ||
      As<SourceCleared>(input) != nullptr) {
    media = ResetMedia(media, view.now_us);
  } else if (As<PlaybackStarted>(input) != nullptr) {
    media.paused = false;
  } else if (As<PlaybackPaused>(input) != nullptr ||
             As<PlayRejected>(input) != nullptr ||
             As<PlaybackEnded>(input) != nullptr) {
    media.paused = true;
  } else if (As<Seeking>(input) != nullptr) {
    media.seeking = true;
  } else if (const auto* seeked = As<Seeked>(input)) {
    media.seeking = false;
    JumpTo(media, seeked->position_us, view.now_us);
  } else if (const auto* update = As<TimeUpdate>(input)) {
    ReduceTimeUpdate(media, view, *update);
  } else if (const auto* duration = As<DurationChanged>(input)) {
    ReduceDuration(media, *duration);
  } else if (const auto* seekable = As<SeekableChanged>(input)) {
    media.window_start_us = seekable->start_us;
    media.window_end_us = std::max(seekable->start_us, seekable->end_us);
  } else if (const auto* buffered = As<BufferedChanged>(input)) {
    media.buffered = buffered->ranges;
  } else if (const auto* volume = As<VolumeChanged>(input)) {
    media.volume = std::clamp(volume->volume, 0.0, 1.0);
    media.muted = volume->muted;
  } else if (const auto* rate = As<RateChanged>(input)) {
    media.rate = std::max(rate->rate, 0.0);
    media.resample_ratio = std::max(rate->resample_ratio, 0.0);
  } else if (const auto* timer = As<TimerFired>(input)) {
    if (timer->kind == TimerKind::kValueText &&
        timer->generation == media.value_timer) {
      media.value_position_us = ExtrapolatePlayhead(media, view.now_us);
      media.value_timer = 0;
    }
  }
}

Media ReduceMedia(const Media& previous, const Input& input, const View& view,
                  Fx& fx) {
  Media media = previous;
  ReduceMediaIntent(media, view, input, fx);
  ReduceMediaEvent(media, view, input);
  return media;
}

PlaybackRegion EnterLoading(const View& view, Fx& fx) {
  return playback::Loading{
      .spinner = false,
      .timer = fx.Arm(TimerKind::kSpinner,
                      MillisToMicros(view.config->spinner_delay_ms))};
}

PlaybackRegion EnterBuffering(const View& view, Fx& fx) {
  return playback::Buffering{
      .spinner = false,
      .timer = fx.Arm(TimerKind::kSpinner,
                      MillisToMicros(view.config->spinner_delay_ms))};
}

PlaybackRegion ReducePlaybackEvent(const PlaybackRegion& region,
                                   const View& view, const Input& input,
                                   Fx& fx) {
  const bool waiting_for_data =
      Is<playback::Loading>(region) || Is<playback::Buffering>(region);
  const bool paused = view.next->media.paused;
  if (As<LoadStarted>(input) != nullptr) {
    return Is<playback::Loading>(region) ? region : EnterLoading(view, fx);
  }
  if (As<SourceCleared>(input) != nullptr) {
    return playback::Idle{};
  }
  if (const auto* failed = As<PlaybackFailed>(input)) {
    return playback::Error{.code = failed->code};
  }
  if (As<PlaybackEnded>(input) != nullptr && !Is<playback::Idle>(region) &&
      !Is<playback::Error>(region)) {
    return playback::Ended{};
  }
  if (As<CanPlay>(input) != nullptr && waiting_for_data) {
    return paused ? PlaybackRegion{playback::Paused{}}
                  : PlaybackRegion{playback::Playing{}};
  }
  if (As<PlaybackStarted>(input) != nullptr &&
      (waiting_for_data || Is<playback::Paused>(region) ||
       Is<playback::Ended>(region))) {
    return playback::Playing{};
  }
  if (As<Waiting>(input) != nullptr && Is<playback::Playing>(region) &&
      !paused) {
    return EnterBuffering(view, fx);
  }
  if (const auto* timer = As<TimerFired>(input);
      timer != nullptr && timer->kind == TimerKind::kSpinner) {
    if (const auto* loading = std::get_if<playback::Loading>(&region);
        loading != nullptr && loading->timer == timer->generation) {
      return playback::Loading{.spinner = true, .timer = loading->timer};
    }
    if (const auto* buffering = std::get_if<playback::Buffering>(&region);
        buffering != nullptr && buffering->timer == timer->generation) {
      return playback::Buffering{.spinner = true, .timer = buffering->timer};
    }
  }
  return region;
}

PlaybackRegion ReducePlayback(const PlaybackRegion& region, const View& view,
                              const Input& input, Fx& fx) {
  PlaybackRegion next = ReducePlaybackEvent(region, view, input, fx);
  const bool was_paused = view.prev->media.paused;
  const bool paused = view.next->media.paused;
  if (was_paused == paused) {
    return next;
  }
  if (!paused && (Is<playback::Paused>(next) || Is<playback::Ended>(next))) {
    return playback::Playing{};
  }
  if (paused &&
      (Is<playback::Playing>(next) || Is<playback::Buffering>(next))) {
    return playback::Paused{};
  }
  return next;
}

AdRegion ReduceAd(const AdRegion& region, const Input& input) {
  if (const auto* started = As<AdBreakStarted>(input)) {
    return ad::Break{
        .owner = started->owner,
        .skip = started->skippable ? AdSkip::kWaiting : AdSkip::kNotSkippable};
  }
  if (As<AdBreakEnded>(input) != nullptr) {
    return ad::Content{};
  }
  if (const auto* adbreak = std::get_if<ad::Break>(&region);
      adbreak != nullptr && As<AdSkipAvailable>(input) != nullptr &&
      adbreak->skip == AdSkip::kWaiting) {
    return ad::Break{.owner = adbreak->owner, .skip = AdSkip::kAvailable};
  }
  return region;
}

void SendFastSeek(scrub::Scrubbing& scrubbing, const Media& media, Fx& fx) {
  scrubbing.last_sent = scrubbing.fraction;
  scrubbing.outstanding = true;
  fx.Emit(
      PlayerSeek{.position_us = FractionToPosition(media, scrubbing.fraction),
                 .precise = false});
}

ScrubRegion ReduceScrub(const ScrubRegion& region, const View& view,
                        const Input& input, Fx& fx) {
  const Media& media = view.next->media;
  if (const auto* begin = As<ScrubBegin>(input)) {
    scrub::Scrubbing scrubbing{.fraction =
                                   std::clamp(begin->fraction, 0.0, 1.0)};
    SendFastSeek(scrubbing, media, fx);
    return scrubbing;
  }
  const auto* current = std::get_if<scrub::Scrubbing>(&region);
  if (current == nullptr) {
    return region;
  }
  scrub::Scrubbing scrubbing = *current;
  if (As<ScrubEnd>(input) != nullptr || As<ScrubCancel>(input) != nullptr) {
    return scrub::Idle{};
  }
  if (const auto* move = As<ScrubMove>(input)) {
    scrubbing.fraction = std::clamp(move->fraction, 0.0, 1.0);
    if (!scrubbing.outstanding) {
      SendFastSeek(scrubbing, media, fx);
    }
  } else if (As<Seeked>(input) != nullptr) {
    scrubbing.outstanding = false;
    if (scrubbing.last_sent != scrubbing.fraction) {
      SendFastSeek(scrubbing, media, fx);
    }
  }
  return scrubbing;
}

MenuRegion CloseOrPop(const menu::Open& open) {
  if (open.path.empty()) {
    return menu::Closed{};
  }
  menu::Open popped = open;
  popped.highlighted = popped.path.back();
  popped.path.pop_back();
  return popped;
}

MenuRegion EnterSubmenu(const menu::Open& open, const MenuNode& item) {
  if (item.kind != MenuItemKind::kSubmenu || item.children.empty()) {
    return open;
  }
  menu::Open entered = open;
  entered.path.push_back(open.highlighted);
  entered.highlighted = 0;
  return entered;
}

MenuRegion ActivateMenuItem(const menu::Open& open, const MenuNode& item,
                            Fx& fx) {
  if (item.kind == MenuItemKind::kSubmenu) {
    return EnterSubmenu(open, item);
  }
  fx.Emit(MenuChoice{.item_id = item.id});
  if (item.kind == MenuItemKind::kChoice) {
    return menu::Closed{};
  }
  return open;
}

MenuRegion NavigateMenu(const menu::Open& open, const View& view,
                        const std::vector<MenuNode>& items,
                        Direction direction) {
  menu::Open moved = open;
  const auto count = static_cast<std::uint32_t>(items.size());
  if (direction == Direction::kDown) {
    moved.highlighted = (open.highlighted + 1) % count;
  } else if (direction == Direction::kUp) {
    moved.highlighted = (open.highlighted + count - 1) % count;
  } else if (Forward(*view.config, direction)) {
    return EnterSubmenu(open, items[open.highlighted]);
  } else {
    return CloseOrPop(open);
  }
  return moved;
}

MenuRegion ReduceMenu(const MenuRegion& region, const View& view,
                      const Input& input, Fx& fx) {
  const auto* open = std::get_if<menu::Open>(&region);
  if (As<OpenMenu>(input) != nullptr) {
    const bool has_items = view.next->menu_root != nullptr &&
                           !view.next->menu_root->children.empty();
    if (open != nullptr || !has_items) {
      return menu::Closed{};
    }
    return menu::Open{};
  }
  if (open == nullptr) {
    return region;
  }
  if (As<MenuBack>(input) != nullptr) {
    return CloseOrPop(*open);
  }
  const std::vector<MenuNode>* items = MenuItems(*view.next, open->path);
  if (items == nullptr || open->highlighted >= items->size()) {
    return menu::Closed{};
  }
  if (const auto* navigate = As<MenuNavigate>(input)) {
    return NavigateMenu(*open, view, *items, navigate->direction);
  }
  if (As<MenuActivate>(input) != nullptr) {
    return ActivateMenuItem(*open, (*items)[open->highlighted], fx);
  }
  return region;
}

void ApplyAdRules(UiState& next) {
  if (InAdBreak(next)) {
    next.menu = menu::Closed{};
    next.scrub = scrub::Idle{};
  }
}

void ValidateMenu(UiState& next) {
  const auto* open = std::get_if<menu::Open>(&next.menu);
  if (open == nullptr) {
    return;
  }
  const std::vector<MenuNode>* items = MenuItems(next, open->path);
  if (items == nullptr || open->highlighted >= items->size()) {
    next.menu = menu::Closed{};
  }
}

PresentationRegion ReducePresentation(const PresentationRegion& region,
                                      const Input& input, Fx& fx) {
  if (const auto* changed = As<PresentationChanged>(input)) {
    switch (changed->mode) {
      case PresentationMode::kInline:
        return presentation::Inline{};
      case PresentationMode::kFullscreen:
        return presentation::Fullscreen{};
      case PresentationMode::kPictureInPicture:
        return presentation::PictureInPicture{};
    }
  }
  if (As<ToggleFullscreen>(input) != nullptr) {
    fx.Emit(RequestPresentation{.mode = Is<presentation::Fullscreen>(region)
                                            ? PresentationMode::kInline
                                            : PresentationMode::kFullscreen});
  }
  return region;
}

chrome::Pinned PinsOf(const ChromeRegion& region) {
  const auto* pinned = std::get_if<chrome::Pinned>(&region);
  return pinned == nullptr ? chrome::Pinned{} : *pinned;
}

std::optional<ChromeRegion> ReduceAutoHideTimer(const ChromeRegion& region,
                                                const View& view,
                                                const TimerFired& timer,
                                                Fx& fx) {
  if (timer.kind != TimerKind::kAutoHide) {
    return std::nullopt;
  }
  const auto* hiding = std::get_if<chrome::AutoHiding>(&region);
  if (hiding == nullptr || hiding->timer != timer.generation) {
    return region;
  }
  if (view.now_us >= hiding->deadline_us) {
    return chrome::Hidden{};
  }
  fx.Emit(StartTimer{.kind = TimerKind::kAutoHide,
                     .generation = hiding->timer,
                     .delay_us = hiding->deadline_us - view.now_us});
  return region;
}

ChromeRegion ReduceChrome(const ChromeRegion& region, const View& view,
                          const Input& input, Fx& fx) {
  if (const auto* timer = As<TimerFired>(input)) {
    if (auto handled = ReduceAutoHideTimer(region, view, *timer, fx)) {
      return *handled;
    }
  }
  const UiState& prev = *view.prev;
  const UiState& next = *view.next;
  const chrome::Pinned before = PinsOf(region);
  chrome::Pinned pins{.menu = MenuIsOpen(next),
                      .pointer = before.pointer,
                      .scrub = Is<scrub::Scrubbing>(next.scrub)};
  if (As<PointerEntered>(input) != nullptr) {
    pins.pointer = true;
  } else if (As<PointerLeft>(input) != nullptr) {
    pins.pointer = false;
  }
  if (As<HideControls>(input) != nullptr && !pins.menu && !pins.scrub) {
    return chrome::Hidden{};
  }
  const bool activity = IsActivity(input);
  const bool pins_changed = pins.menu != MenuIsOpen(prev) ||
                            pins.pointer != before.pointer ||
                            pins.scrub != Is<scrub::Scrubbing>(prev.scrub);
  const bool playing = IsPlaying(next);
  const bool playing_changed = playing != IsPlaying(prev);
  if (!activity && !pins_changed && !playing_changed) {
    return region;
  }
  if (pins.menu || pins.pointer || pins.scrub) {
    return pins;
  }
  if (!playing) {
    return chrome::Shown{};
  }
  if (Is<chrome::Hidden>(region) && !activity) {
    return region;
  }
  const Micros deadline =
      view.now_us + MillisToMicros(view.config->auto_hide_ms);
  if (const auto* hiding = std::get_if<chrome::AutoHiding>(&region)) {
    return chrome::AutoHiding{.deadline_us = deadline, .timer = hiding->timer};
  }
  return chrome::AutoHiding{
      .deadline_us = deadline,
      .timer = fx.Arm(TimerKind::kAutoHide,
                      MillisToMicros(view.config->auto_hide_ms))};
}

std::optional<ControlId> TabTarget(const std::vector<ControlView>& controls,
                                   std::optional<ControlId> from,
                                   bool backward) {
  std::vector<ControlId> order;
  for (const ControlView& control : controls) {
    if (control.focusable) {
      order.push_back(control.id);
    }
  }
  if (order.empty()) {
    return std::nullopt;
  }
  if (!from) {
    return backward ? order.back() : order.front();
  }
  const auto found = std::ranges::find(order, *from);
  const auto index = static_cast<std::size_t>(found - order.begin());
  if (found == order.end()) {
    return order.front();
  }
  const std::size_t count = order.size();
  return order[backward ? (index + count - 1) % count : (index + 1) % count];
}

std::optional<ControlId> EdgeTarget(const std::vector<ControlView>& controls,
                                    ControlId from, Direction direction) {
  for (const FocusEdges& edges : BuildFocusEdges(controls)) {
    if (edges.id != from) {
      continue;
    }
    switch (direction) {
      case Direction::kUp:
        return edges.up;
      case Direction::kDown:
        return edges.down;
      case Direction::kLeft:
        return edges.left;
      case Direction::kRight:
        return edges.right;
    }
  }
  return std::nullopt;
}

std::optional<ControlId> Primary(const std::vector<ControlView>& controls) {
  const ControlView* play = FindControl(controls, ControlId::kPlayPause);
  if (play != nullptr && play->focusable) {
    return play->id;
  }
  return TabTarget(controls, std::nullopt, false);
}

bool Focusable(const std::vector<ControlView>& controls, ControlId id) {
  const ControlView* control = FindControl(controls, id);
  return control != nullptr && control->focusable;
}

std::optional<ControlId> FocusTargetFor(const View& view, const Input& input,
                                        std::optional<ControlId> target,
                                        ControlId restore) {
  const std::vector<ControlView>& controls = *view.controls;
  if (As<FocusChrome>(input) != nullptr) {
    return Focusable(controls, restore) ? std::optional<ControlId>(restore)
                                        : Primary(controls);
  }
  if (const auto* focus_control = As<FocusControl>(input)) {
    return Focusable(controls, focus_control->id)
               ? std::optional<ControlId>(focus_control->id)
               : target;
  }
  if (const auto* move = As<MoveFocus>(input)) {
    const auto edge =
        target ? EdgeTarget(controls, *target, move->direction) : std::nullopt;
    return edge ? edge : target;
  }
  if (const auto* traverse = As<FocusTraverse>(input)) {
    return TabTarget(controls, target, traverse->backward);
  }
  if ((As<AdBreakStarted>(input) != nullptr ||
       As<AdSkipAvailable>(input) != nullptr) &&
      InAdBreak(*view.next) && target &&
      Focusable(controls, ControlId::kSkipAd)) {
    return ControlId::kSkipAd;
  }
  return target;
}

FocusRegion ReduceFocus(const FocusRegion& region, const View& view,
                        const Input& input) {
  const std::vector<ControlView>& controls = *view.controls;
  std::optional<ControlId> target;
  ControlId restore = ControlId::kPlayPause;
  if (const auto* none = std::get_if<focus::None>(&region)) {
    restore = none->restore;
  } else if (const auto* on = std::get_if<focus::On>(&region)) {
    target = on->target;
    restore = on->restore;
  }
  if (!ChromeVisible(*view.next)) {
    return focus::None{.restore = target.value_or(restore)};
  }
  target = FocusTargetFor(view, input, target, restore);
  if (target && !Focusable(controls, *target)) {
    target = NearestFocusable(controls, *target);
  }
  if (!target) {
    return focus::None{.restore = restore};
  }
  return focus::On{.target = *target, .restore = *target};
}

void SyncPlayhead(UiState& next, Micros now_us) {
  const double rate = EffectiveRate(next);
  if (rate == next.media.playhead.rate) {
    return;
  }
  next.media.playhead =
      Playhead{.position_us = ExtrapolatePlayhead(next.media, now_us),
               .rate = rate,
               .anchor_us = now_us};
}

void SampleValue(UiState& next, Micros now_us, Fx& fx) {
  if (!IsPlaying(next)) {
    next.media.value_timer = 0;
    next.media.value_position_us = ExtrapolatePlayhead(next.media, now_us);
  } else if (next.media.value_timer == 0) {
    next.media.value_timer =
        fx.Arm(TimerKind::kValueText, kValueTextIntervalUs);
  }
}

void Announce(UiState& next, std::string key, std::vector<TextArg> args,
              Politeness politeness) {
  next.announcements.push_back(
      Announcement{.sequence = next.next_announcement++,
                   .key = std::move(key),
                   .args = std::move(args),
                   .politeness = politeness});
}

void AnnounceChanges(const UiState& prev, UiState& next) {
  if (prev.media.paused != next.media.paused && PlaybackAvailable(prev) &&
      PlaybackAvailable(next)) {
    Announce(next, next.media.paused ? "announce.pause" : "announce.play", {},
             Politeness::kPolite);
  }
  if (prev.media.seeking && !next.media.seeking) {
    Announce(next, "announce.seeked",
             {TextArg{.name = "position_s",
                      .value = next.media.playhead.position_us / 1'000'000}},
             Politeness::kPolite);
  }
  if (const auto* error = std::get_if<playback::Error>(&next.playback);
      error != nullptr && !Is<playback::Error>(prev.playback)) {
    Announce(next, "announce.error",
             {TextArg{.name = "code",
                      .value = static_cast<std::int64_t>(error->code)}},
             Politeness::kAssertive);
  }
  if (!InAdBreak(prev) && InAdBreak(next)) {
    Announce(next, "announce.ad_started", {}, Politeness::kPolite);
  }
}

void EmitActionEffects(const UiState& next, const Input& input, Fx& fx) {
  if (As<PassBack>(input) != nullptr) {
    fx.Emit(BackToApp{});
  } else if (As<SkipAd>(input) != nullptr) {
    fx.Emit(SkipAdRequested{});
  } else if (const auto* custom = As<ActivateCustom>(input)) {
    for (const CustomControl& control : next.custom) {
      if (control.id == custom->id) {
        fx.Emit(CustomAction{.name = control.name});
      }
    }
  }
}

}

UiState InitialState() {
  UiState state;
  state.playback = playback::Idle{};
  state.chrome = chrome::Shown{};
  state.menu = menu::Closed{};
  state.ad = ad::Content{};
  state.presentation = presentation::Inline{};
  state.scrub = scrub::Idle{};
  state.focus = focus::None{};
  return state;
}

Step Reduce(const UiState& previous, const UiConfig& config, Micros now_us,
            const Input& input) {
  Step step{.state = previous, .effects = {}};
  UiState& next = step.state;
  const std::vector<ControlView> before = DeriveControls(previous, config);
  View view{.prev = &previous,
            .next = &next,
            .config = &config,
            .controls = &before,
            .now_us = now_us};
  const std::optional<Input> normalized = Normalize(view, input);
  if (!normalized) {
    return step;
  }
  const Input& in = *normalized;
  Fx fx{.effects = {}, .next_timer = previous.next_timer};
  ReduceCatalog(next, in);
  next.media = ReduceMedia(previous.media, in, view, fx);
  next.playback = ReducePlayback(previous.playback, view, in, fx);
  next.ad = ReduceAd(previous.ad, in);
  next.scrub = ReduceScrub(previous.scrub, view, in, fx);
  next.menu = ReduceMenu(previous.menu, view, in, fx);
  ApplyAdRules(next);
  ValidateMenu(next);
  next.presentation = ReducePresentation(previous.presentation, in, fx);
  next.chrome = ReduceChrome(previous.chrome, view, in, fx);
  const std::vector<ControlView> after = DeriveControls(next, config);
  view.controls = &after;
  next.focus = ReduceFocus(previous.focus, view, in);
  EmitActionEffects(next, in, fx);
  SyncPlayhead(next, now_us);
  SampleValue(next, now_us, fx);
  AnnounceChanges(previous, next);
  next.next_timer = fx.next_timer;
  step.effects = std::move(fx.effects);
  return step;
}

}
