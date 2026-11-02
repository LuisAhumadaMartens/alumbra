#include "alumbra/desktop_ui/layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/icons.h"
#include "alumbra/desktop_ui/strings.h"
#include "alumbra/desktop_ui/structure.h"
#include "alumbra/desktop_ui/style.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {
namespace {

using umbra::ui::ControlId;
using umbra::ui::ControlKind;
using umbra::ui::ControlView;
using umbra::ui::Slot;

constexpr float kVolumeSliderTargets = 3;

struct Item {
  const ControlView* control = nullptr;
  float width = 0;
};

float Total(const std::vector<Item>& items, float gap) {
  float total = 0;
  for (const Item& item : items) {
    total += item.width;
  }
  if (!items.empty()) {
    total += gap * static_cast<float>(items.size() - 1);
  }
  return total;
}

float LineHeight(const FontMetrics& metrics, float font_size) {
  return (metrics.ascent + metrics.descent) * font_size;
}

float BaselineIn(const FontMetrics& metrics, float font_size, float top,
                 float height) {
  const float line = LineHeight(metrics, font_size);
  return top + ((height - line) / 2) + (metrics.ascent * font_size);
}

ControlLayout PlaceControl(const ControlView& control, const Rect& hit,
                           const Style& style) {
  ControlLayout placed;
  placed.id = control.id;
  placed.kind = control.kind;
  placed.hit = hit;
  placed.icon_id = IconFromToken(control.icon_token);
  placed.slider = control.kind == ControlKind::kSlider;
  if (placed.slider) {
    const float inset = style.medium / 2;
    placed.track = Rect{hit.x + inset, hit.Centre().y - (style.tight / 2),
                        hit.width - (2 * inset), style.tight};
  } else {
    const float side = style.large;
    placed.icon = Rect{hit.Centre().x - (side / 2), hit.Centre().y - (side / 2),
                       side, side};
  }
  return placed;
}

TextBlock MakeBlock(const TextShaper& shaper, const StringCatalogue& catalogue,
                    std::string text, float font_size) {
  TextBlock block;
  block.shaped = shaper.Shape(text, catalogue.Direction());
  block.text = std::move(text);
  block.font_size = font_size;
  return block;
}

std::optional<ErrorLayout> PlaceError(const LayoutInput& input,
                                      const Style& style,
                                      float layout_band_height) {
  const umbra::ui::ModesSection& modes = input.view_model->modes;
  if (!modes.error.has_value()) {
    return std::nullopt;
  }
  const FontMetrics metrics = input.shaper->Metrics();
  ErrorLayout error;
  error.title =
      MakeBlock(*input.shaper, *input.catalogue,
                input.catalogue->Resolve("error.title", {}), style.font_title);
  error.message = MakeBlock(
      *input.shaper, *input.catalogue,
      input.catalogue->Resolve(
          "error.message",
          {umbra::ui::TextArg{
              .name = "code",
              .value = static_cast<std::int64_t>(modes.error->code)}}),
      style.font_body);
  const float title_width = error.title.shaped.advance * style.font_title;
  const float message_width = error.message.shaped.advance * style.font_body;
  const float content_width = std::max(title_width, message_width);
  const float panel_width =
      std::max(0.0F, std::min(content_width + (2 * style.large),
                              input.viewport.width - (2 * style.medium)));
  const float title_line = LineHeight(metrics, style.font_title);
  const float message_line = LineHeight(metrics, style.font_body);
  const float panel_height = style.large + style.target + style.medium +
                             title_line + style.small + message_line +
                             style.large;
  const float free_height = input.viewport.height - layout_band_height;
  error.panel = Rect{(input.viewport.width - panel_width) / 2,
                     std::max(0.0F, (free_height - panel_height) / 2),
                     panel_width, panel_height};
  const bool mirrored =
      input.catalogue->Direction() == TextDirection::kRightToLeft;
  const auto start_x = [&](float width) {
    return mirrored ? error.panel.Right() - style.large - width
                    : error.panel.x + style.large;
  };
  error.icon = Rect{start_x(style.target), error.panel.y + style.large,
                    style.target, style.target};
  const float title_top = error.icon.Bottom() + style.medium;
  error.title.origin = Point{start_x(title_width),
                             title_top + (metrics.ascent * style.font_title)};
  const float message_top = title_top + title_line + style.small;
  error.message.origin = Point{
      start_x(message_width), message_top + (metrics.ascent * style.font_body)};
  return error;
}

}

const ControlLayout* Layout::Find(ControlId id) const {
  for (const ControlLayout& control : controls) {
    if (control.id == id) {
      return &control;
    }
  }
  return nullptr;
}

std::optional<ControlId> Layout::HitTest(Point point) const {
  for (const ControlLayout& control : controls) {
    if (control.hit.Contains(point)) {
      return control.id;
    }
  }
  return std::nullopt;
}

double SliderFraction(const ControlLayout& control, float x) {
  if (control.track.width <= 0) {
    return 0;
  }
  return std::clamp(
      static_cast<double>((x - control.track.x) / control.track.width), 0.0,
      1.0);
}

std::string ClockText(const umbra::ui::SeekBarSection& seek_bar,
                      std::int64_t position_us,
                      const StringCatalogue& catalogue, bool reserve) {
  if (seek_bar.live) {
    return catalogue.Resolve("time.live", {});
  }
  const bool with_hours =
      seek_bar.duration_known && seek_bar.duration_us >= kMicrosecondsPerHour;
  const std::string elapsed = FormatClock(position_us, with_hours);
  const std::string total = FormatClock(
      seek_bar.duration_known ? seek_bar.duration_us : 0, with_hours);
  std::string text = absl::StrCat(elapsed, " / ", total);
  if (reserve) {
    for (char& character : text) {
      if (character >= '0' && character <= '9') {
        character = '0';
      }
    }
  }
  return text;
}

Layout ComputeLayout(const LayoutInput& input) {
  const Style style = StyleOf(*input.tokens);
  const umbra::ui::ViewModel& view_model = *input.view_model;
  const FontMetrics metrics = input.shaper->Metrics();
  Layout layout;
  layout.viewport = input.viewport;
  const float width = input.viewport.width;
  const float height = input.viewport.height;
  const float pad = style.medium;
  const float seek_height = style.target;
  const float band_height =
      style.small + seek_height + style.target + style.small;
  layout.band = Rect{0, height - band_height, width, band_height};
  const float seek_y = layout.band.y + style.small;
  const float row_y = seek_y + seek_height;

  std::vector<Item> leading;
  std::vector<Item> trailing;
  for (const ControlView& control : view_model.controls.controls) {
    if (!control.visible) {
      continue;
    }
    if (control.slot == Slot::kProgress) {
      layout.controls.push_back(PlaceControl(
          control, Rect{pad, seek_y, width - (2 * pad), seek_height}, style));
    } else if (control.slot == Slot::kLeading) {
      leading.push_back(Item{.control = &control,
                             .width = control.kind == ControlKind::kSlider
                                          ? style.target * kVolumeSliderTargets
                                          : style.target});
    } else {
      trailing.push_back(Item{.control = &control, .width = style.target});
    }
  }
  const bool has_clock = std::ranges::any_of(
      view_model.controls.controls, [](const ControlView& control) {
        return control.visible && control.slot == Slot::kProgress;
      });
  const std::string clock =
      ClockText(view_model.seek_bar, 0, *input.catalogue, true);
  const float clock_width =
      input.shaper->Shape(clock, TextDirection::kLeftToRight).advance *
      style.font_body;
  if (has_clock) {
    leading.push_back(Item{.control = nullptr, .width = clock_width});
  }

  const float gap = style.tight;
  const float trailing_width = Total(trailing, gap);
  const float trailing_x = width - pad - trailing_width;
  const float budget = trailing_x - gap - pad;
  const auto drop_first = [&](auto predicate) {
    const auto found = std::ranges::find_if(leading, predicate);
    if (found != leading.end()) {
      leading.erase(found);
    }
  };
  if (Total(leading, gap) > budget) {
    drop_first([](const Item& item) {
      return item.control != nullptr &&
             item.control->kind == ControlKind::kSlider;
    });
  }
  if (Total(leading, gap) > budget) {
    drop_first([](const Item& item) { return item.control == nullptr; });
  }
  while (!leading.empty() && Total(leading, gap) > budget) {
    leading.pop_back();
  }

  float cursor = pad;
  for (const Item& item : leading) {
    const Rect hit{cursor, row_y, item.width, style.target};
    if (item.control != nullptr) {
      layout.controls.push_back(PlaceControl(*item.control, hit, style));
    } else {
      layout.time_box = hit;
      layout.time_font_size = style.font_body;
      layout.time_baseline =
          BaselineIn(metrics, style.font_body, row_y, style.target);
    }
    cursor += item.width + gap;
  }
  cursor = trailing_x;
  for (const Item& item : trailing) {
    layout.controls.push_back(PlaceControl(
        *item.control, Rect{cursor, row_y, item.width, style.target}, style));
    cursor += item.width + gap;
  }

  layout.error = PlaceError(input, style, band_height);
  if (view_model.modes.spinner) {
    SpinnerLayout spinner;
    spinner.centre = Point{width / 2, height / 2};
    spinner.radius = style.target / 2;
    spinner.dot = style.small;
    layout.spinner = spinner;
  }
  return layout;
}

}
