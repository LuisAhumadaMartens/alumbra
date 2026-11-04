#include "alumbra/desktop_ui/compose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ratio>
#include <string>
#include <utility>

#include "alumbra/desktop_ui/animation.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/icons.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/structure.h"
#include "alumbra/desktop_ui/style.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "umbra/player/text_track.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {
namespace {

using umbra::ui::ControlId;
using umbra::ui::ControlView;
using umbra::ui::Micros;

struct Window {
  Micros start = 0;
  Micros end = 0;
};

void PaintCaptions(DrawList* list, const FrameInput& input,
                   const Style& style) {
  if (input.captions == nullptr || input.captions->empty()) {
    return;
  }
  const double media_time = static_cast<double>(input.media_time_us) / 1.0e6;
  for (const umbra::player::TextCue& cue : *input.captions) {
    if (media_time < cue.start_time || media_time >= cue.end_time) {
      continue;
    }
    const float width =
        input.viewport.width *
        static_cast<float>(
            std::clamp(cue.size * std::centi::num / std::centi::den, 0.0, 1.0));
    const float height = style.large + style.medium;
    const float x = (input.viewport.width - width) / 2.0F;
    const float y =
        (input.viewport.height *
         static_cast<float>(std::clamp(
             cue.position * std::centi::num / std::centi::den, 0.0, 1.0))) -
        height;
    const Rect box{x, y, width, height};
    list->FillRoundedRect(box, style.radius_small, style.scrim);
    float glyph_x = box.x + style.small;
    const float baseline = box.Bottom() - style.small;
    for (const umbra::player::StyledTextRun& run : cue.runs) {
      const ShapedText shaped = input.shaper->Shape(
          run.text, cue.right_to_left ? TextDirection::kRightToLeft
                                      : TextDirection::kLeftToRight);
      const float size = run.font_size > 0 ? static_cast<float>(run.font_size)
                                           : style.font_caption;
      const std::uint32_t channel_base =
          std::numeric_limits<std::uint8_t>::max() + 1;
      const std::uint32_t channel_scale =
          std::numeric_limits<std::uint8_t>::max();
      const std::uint32_t argb = run.foreground_argb;
      const Rgba colour =
          run.has_foreground_color
              ? Rgba{static_cast<float>((argb / (channel_base * channel_base)) %
                                        channel_base) /
                         channel_scale,
                     static_cast<float>((argb / channel_base) % channel_base) /
                         channel_scale,
                     static_cast<float>(argb % channel_base) / channel_scale,
                     static_cast<float>(argb) /
                         (static_cast<float>(channel_base) * channel_base *
                          channel_base * channel_scale)}
              : style.text_primary;
      for (const ShapedGlyph& glyph : shaped.glyphs) {
        list->DrawGlyph(glyph.glyph,
                        Point{glyph_x + (glyph.x_offset * size),
                              baseline - (glyph.y_offset * size)},
                        size, colour);
        glyph_x += (glyph.x_advance * size);
      }
    }
  }
}

Window WindowOf(const umbra::ui::SeekBarSection& seek_bar) {
  if (seek_bar.window_end_us > seek_bar.window_start_us) {
    return Window{.start = seek_bar.window_start_us,
                  .end = seek_bar.window_end_us};
  }
  return Window{.start = 0,
                .end = seek_bar.duration_known ? seek_bar.duration_us : 0};
}

double FractionOf(const Window& window, Micros position_us) {
  if (window.end <= window.start) {
    return 0;
  }
  return std::clamp(static_cast<double>(position_us - window.start) /
                        static_cast<double>(window.end - window.start),
                    0.0, 1.0);
}

class Painter {
 public:
  explicit Painter(const FrameInput& input)
      : input_(input),
        style_(StyleOf(*input.tokens)),
        view_model_(input.view_model),
        layout_(input.layout) {}

  DrawList Run() {
    PaintCaptions(&list_, input_, style_);
    if (layout_->error.has_value()) {
      PaintError(*layout_->error);
    }
    if (layout_->spinner.has_value()) {
      PaintSpinner(*layout_->spinner);
    }
    if (input_.chrome_alpha > 0) {
      PaintChrome();
    }
    return std::move(list_);
  }

 private:
  [[nodiscard]] bool Focused(ControlId id) const {
    return view_model_->focus.target.has_value() &&
           *view_model_->focus.target == id;
  }

  [[nodiscard]] bool Hovered(ControlId id) const {
    return input_.interaction.hover.has_value() &&
           *input_.interaction.hover == id;
  }

  [[nodiscard]] bool Pressed(ControlId id) const {
    return Hovered(id) && input_.interaction.pressed.has_value() &&
           *input_.interaction.pressed == id;
  }

  [[nodiscard]] Micros Position() const {
    return ExtrapolatedPosition(view_model_->seek_bar, input_.now_us);
  }

  [[nodiscard]] const ButtonColours& ColoursFor(ControlId id,
                                                bool disabled) const {
    if (disabled) {
      return style_.disabled;
    }
    if (Pressed(id)) {
      return style_.pressed;
    }
    if (Hovered(id)) {
      return style_.hover;
    }
    if (Focused(id)) {
      return style_.focus;
    }
    return style_.idle;
  }

  void PaintRing(const Rect& around, float radius) {
    const float gap = style_.tight / 2;
    list_.StrokeRoundedRect(around.Inflated(gap * 2), radius + (gap * 2), gap,
                            Scaled(style_.focus_ring, input_.chrome_alpha));
  }

  void PaintText(const ShapedText& shaped, Point origin, float size,
                 const Rgba& color) {
    float x = origin.x;
    for (const ShapedGlyph& glyph : shaped.glyphs) {
      list_.DrawGlyph(glyph.glyph,
                      Point{x + (glyph.x_offset * size),
                            origin.y - (glyph.y_offset * size)},
                      size, color);
      x += glyph.x_advance * size;
    }
  }

  void PaintButton(const ControlLayout& control, const ControlView& view) {
    const float alpha = input_.chrome_alpha;
    const bool disabled = !view.enabled;
    const ButtonColours& colours = ColoursFor(control.id, disabled);
    list_.FillRoundedRect(control.hit, style_.button_radius,
                          Scaled(colours.background, alpha));
    const float icon_alpha = disabled ? alpha * style_.disabled_opacity : alpha;
    list_.DrawIcon(control.icon, control.icon_id,
                   Scaled(colours.foreground, icon_alpha));
    if (Focused(control.id)) {
      PaintRing(control.hit, style_.button_radius);
    }
  }

  void PaintSeek(const ControlLayout& control, const ControlView& view) {
    const float alpha = input_.chrome_alpha;
    const umbra::ui::SeekBarSection& seek_bar = view_model_->seek_bar;
    const Window window = WindowOf(seek_bar);
    const Rect& track = control.track;
    const float radius = track.height / 2;
    list_.FillRoundedRect(track, radius, Scaled(style_.track, alpha));
    for (const umbra::ui::TimeRange& range : seek_bar.buffered) {
      const double from = FractionOf(window, range.start_us);
      const double to = FractionOf(window, range.end_us);
      if (to > from) {
        list_.FillRoundedRect(
            Rect{track.x + (track.width * static_cast<float>(from)), track.y,
                 track.width * static_cast<float>(to - from), track.height},
            radius, Scaled(style_.buffered, alpha));
      }
    }
    const double fraction = seek_bar.scrub.has_value()
                                ? seek_bar.scrub->fraction
                                : FractionOf(window, Position());
    const float played_width = track.width * static_cast<float>(fraction);
    if (played_width > 0) {
      list_.FillRoundedRect(Rect{track.x, track.y, played_width, track.height},
                            radius, Scaled(style_.played, alpha));
    }
    const bool engaged = Hovered(control.id) || Focused(control.id) ||
                         seek_bar.scrub.has_value();
    if (engaged && view.enabled) {
      const float diameter = style_.medium;
      list_.FillRoundedRect(
          Rect{track.x + played_width - (diameter / 2),
               track.Centre().y - (diameter / 2), diameter, diameter},
          diameter / 2, Scaled(style_.accent, alpha));
    }
    if (Focused(control.id)) {
      PaintRing(track.Inflated(style_.small), style_.radius_pill);
    }
  }

  void PaintVolume(const ControlLayout& control, const ControlView& view) {
    const float alpha = input_.chrome_alpha;
    const Rect& track = control.track;
    const float radius = track.height / 2;
    list_.FillRoundedRect(track, radius, Scaled(style_.track, alpha));
    const double span = view.value_max - view.value_min;
    const double fraction =
        span > 0
            ? std::clamp((view.value_now - view.value_min) / span, 0.0, 1.0)
            : 0.0;
    const float filled = track.width * static_cast<float>(fraction);
    if (filled > 0) {
      list_.FillRoundedRect(Rect{track.x, track.y, filled, track.height},
                            radius, Scaled(style_.played, alpha));
    }
    const float diameter = style_.medium;
    list_.FillRoundedRect(
        Rect{track.x + filled - (diameter / 2),
             track.Centre().y - (diameter / 2), diameter, diameter},
        diameter / 2, Scaled(style_.accent, alpha));
    if (Focused(control.id)) {
      PaintRing(track.Inflated(style_.small), style_.radius_pill);
    }
  }

  void PaintClock() {
    if (!layout_->time_box.has_value()) {
      return;
    }
    const umbra::ui::SeekBarSection& seek_bar = view_model_->seek_bar;
    const Micros shown =
        seek_bar.scrub.has_value() ? seek_bar.scrub->position_us : Position();
    const std::string text =
        ClockText(seek_bar, shown, *input_.catalogue, false);
    const ShapedText shaped =
        input_.shaper->Shape(text, TextDirection::kLeftToRight);
    PaintText(shaped, Point{layout_->time_box->x, layout_->time_baseline},
              layout_->time_font_size,
              Scaled(style_.text_primary, input_.chrome_alpha));
  }

  void PaintChrome() {
    list_.FillRoundedRect(layout_->band, 0,
                          Scaled(style_.scrim, input_.chrome_alpha));
    for (const ControlLayout& control : layout_->controls) {
      const ControlView* view =
          umbra::ui::FindControl(view_model_->controls.controls, control.id);
      if (view == nullptr) {
        continue;
      }
      if (control.id == ControlId::kSeekBar) {
        PaintSeek(control, *view);
      } else if (control.slider) {
        PaintVolume(control, *view);
      } else {
        PaintButton(control, *view);
      }
    }
    PaintClock();
    if (layout_->ad_position.has_value()) {
      const TextBlock& block = *layout_->ad_position;
      PaintText(block.shaped, block.origin, block.font_size,
                Scaled(style_.text_primary, input_.chrome_alpha));
    }
    if (layout_->ad_countdown.has_value()) {
      const TextBlock& block = *layout_->ad_countdown;
      PaintText(block.shaped, block.origin, block.font_size,
                Scaled(style_.text_primary, input_.chrome_alpha));
    }
  }

  void PaintSpinner(const SpinnerLayout& spinner) {
    const float phase = SpinnerPhase(input_.now_us, style_.spinner_period_us);
    const auto count = static_cast<float>(kSpinnerDots);
    const float head = phase * count;
    for (std::size_t index = 0; index < kSpinnerDots; ++index) {
      const float angle = 2 * kPi * static_cast<float>(index) / count;
      const Point centre{spinner.centre.x + (spinner.radius * std::sin(angle)),
                         spinner.centre.y - (spinner.radius * std::cos(angle))};
      float behind = head - static_cast<float>(index);
      behind -= std::floor(behind / count) * count;
      const float intensity = 1 - (behind / count);
      list_.FillRoundedRect(
          Rect{centre.x - (spinner.dot / 2), centre.y - (spinner.dot / 2),
               spinner.dot, spinner.dot},
          spinner.dot / 2, Scaled(style_.accent, (intensity + 1) / 2));
    }
  }

  void PaintError(const ErrorLayout& error) {
    list_.FillAndStrokeRoundedRect(error.panel, style_.radius_large,
                                   style_.elevated, 1, style_.border);
    list_.PushClip(error.panel);
    list_.DrawIcon(error.icon, IconId::kError, style_.text_primary);
    PaintText(error.title.shaped, error.title.origin, error.title.font_size,
              style_.text_primary);
    PaintText(error.message.shaped, error.message.origin,
              error.message.font_size, style_.text_secondary);
    list_.PopClip();
  }

  FrameInput input_;
  Style style_;
  const umbra::ui::ViewModel* view_model_;
  const Layout* layout_;
  DrawList list_;
};

}

std::int64_t ExtrapolatedPosition(const umbra::ui::SeekBarSection& seek_bar,
                                  std::int64_t now_us) {
  const umbra::ui::Playhead& head = seek_bar.playhead;
  const auto elapsed = static_cast<double>(now_us - head.anchor_us);
  const auto moved = static_cast<Micros>(std::llround(head.rate * elapsed));
  const Window window = WindowOf(seek_bar);
  const Micros position = head.position_us + moved;
  if (window.end > window.start) {
    return std::clamp(position, window.start, window.end);
  }
  return std::max<Micros>(position, 0);
}

double PlayheadFraction(const umbra::ui::SeekBarSection& seek_bar,
                        std::int64_t position_us) {
  return FractionOf(WindowOf(seek_bar), position_us);
}

DrawList Compose(const FrameInput& input) { return Painter(input).Run(); }

}
