#include "alumbra/desktop_ui/desktop_ui.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/animation.h"
#include "alumbra/desktop_ui/compose.h"
#include "alumbra/desktop_ui/embedded_font.h"
#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/gpu_renderer.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/style.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/machine.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace alumbra::desktop_ui {

absl::StatusOr<std::unique_ptr<DesktopUi>> DesktopUi::Create(
    penumbra::desktop::GpuPresenter& presenter, umbra::ui::UiMachine& machine,
    const DesktopUiOptions& options) {
  std::unique_ptr<DesktopUi> ui(new DesktopUi(presenter, machine, options));
  absl::StatusOr<std::unique_ptr<FontFace>> face =
      FontFace::Create(EmbeddedFontData());
  if (!face.ok()) {
    return face.status();
  }
  ui->shared_face_ = *std::move(face);
  absl::StatusOr<std::unique_ptr<TextShaper>> shaper =
      TextShaper::Create(EmbeddedFontData());
  if (!shaper.ok()) {
    return shaper.status();
  }
  ui->shaper_ = *std::move(shaper);
  absl::StatusOr<std::shared_ptr<UiGpuRenderer>> renderer =
      UiGpuRenderer::Create(presenter.Context(), ui->shared_face_);
  if (!renderer.ok()) {
    return renderer.status();
  }
  ui->renderer_ = *std::move(renderer);
  presenter.SetOverlay(ui->renderer_);
  return ui;
}

DesktopUi::DesktopUi(penumbra::desktop::GpuPresenter& presenter,
                     umbra::ui::UiMachine& machine,
                     const DesktopUiOptions& options)
    : presenter_(presenter),
      machine_(machine),
      options_(options),
      tokens_(design_tokens::Resolve(options.context, options.overrides)),
      catalogue_(options.locale) {}

DesktopUi::~DesktopUi() { presenter_.SetOverlay(nullptr); }

void DesktopUi::SetTokens(const design_tokens::Context& context,
                          const design_tokens::Overrides& overrides) {
  options_.context = context;
  options_.overrides = overrides;
  tokens_ = design_tokens::Resolve(context, overrides);
}

void DesktopUi::Apply(const std::vector<umbra::ui::SequencedEffect>& effects) {
  std::vector<umbra::ui::SequencedEffect> forwarded;
  for (const umbra::ui::SequencedEffect& sequenced : effects) {
    if (const auto* timer =
            std::get_if<umbra::ui::StartTimer>(&sequenced.effect)) {
      timers_.push_back(PendingTimer{
          .due_us = machine_.NowMicros() + timer->delay_us,
          .timer = umbra::ui::TimerFired{.kind = timer->kind,
                                         .generation = timer->generation}});
      continue;
    }
    if (const auto* presentation =
            std::get_if<umbra::ui::RequestPresentation>(&sequenced.effect)) {
      if (options_.window != nullptr) {
        SDL_SetWindowFullscreen(
            options_.window,
            presentation->mode == umbra::ui::PresentationMode::kFullscreen);
      }
    }
    forwarded.push_back(sequenced);
  }
  if (!forwarded.empty() && sink_) {
    sink_(forwarded);
  }
}

void DesktopUi::Dispatch(const std::vector<umbra::ui::Input>& inputs) {
  for (const umbra::ui::Input& input : inputs) {
    Apply(machine_.Dispatch(input));
  }
}

bool DesktopUi::HandleEvent(const SDL_Event& event) {
  const bool chrome_visible =
      snapshot_ != nullptr && snapshot_->modes.chrome_visible;
  const std::vector<umbra::ui::Input> inputs =
      translator_.Translate(event, layout_, chrome_visible);
  Dispatch(inputs);
  return !inputs.empty();
}

std::optional<std::int64_t> DesktopUi::NextTimerDeadlineUs() const {
  std::optional<std::int64_t> earliest;
  for (const PendingTimer& pending : timers_) {
    if (!earliest.has_value() || pending.due_us < *earliest) {
      earliest = pending.due_us;
    }
  }
  return earliest;
}

bool DesktopUi::Frame(std::int64_t now_us) {
  for (;;) {
    std::optional<std::size_t> due;
    for (std::size_t index = 0; index < timers_.size(); ++index) {
      if (timers_.at(index).due_us <= machine_.NowMicros()) {
        due = index;
        break;
      }
    }
    if (!due.has_value()) {
      break;
    }
    const umbra::ui::TimerFired fired = timers_.at(*due).timer;
    timers_.erase(timers_.begin() + static_cast<std::ptrdiff_t>(*due));
    Dispatch({umbra::ui::Input{fired}});
  }
  std::shared_ptr<const umbra::ui::ViewModel> taken = machine_.TakeSnapshot();
  if (taken != nullptr) {
    snapshot_ = std::move(taken);
  }
  if (snapshot_ == nullptr) {
    return false;
  }
  const penumbra::desktop::PixelSize pixels = presenter_.TargetPixels();
  const float density = presenter_.PixelDensity();
  if (pixels.width <= 0 || pixels.height <= 0 || density <= 0) {
    return false;
  }
  const Size logical{static_cast<float>(pixels.width) / density,
                     static_cast<float>(pixels.height) / density};
  LayoutInput layout_input;
  layout_input.viewport = logical;
  layout_input.view_model = snapshot_.get();
  layout_input.tokens = &tokens_;
  layout_input.shaper = shaper_.get();
  layout_input.catalogue = &catalogue_;
  layout_ = ComputeLayout(layout_input);
  const Style style = StyleOf(tokens_);
  FrameInput input;
  input.view_model = snapshot_.get();
  input.layout = &layout_;
  input.tokens = &tokens_;
  input.shaper = shaper_.get();
  input.catalogue = &catalogue_;
  input.now_us = now_us;
  input.interaction = translator_.GetInteraction();
  input.chrome_alpha =
      fade_.Advance(snapshot_->modes.chrome_visible, now_us, style);
  draw_list_ = Compose(input);
  renderer_->SetDrawList(draw_list_);
  const umbra::ui::SeekBarSection& seek = snapshot_->seek_bar;
  animating_ = snapshot_->modes.spinner || !fade_.Settled() ||
               seek.scrub.has_value() ||
               (snapshot_->modes.chrome_visible && seek.playhead.rate != 0);
  return animating_;
}

}
