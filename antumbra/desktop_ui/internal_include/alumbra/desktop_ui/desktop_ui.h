#ifndef ALUMBRA_DESKTOP_UI_DESKTOP_UI_H_
#define ALUMBRA_DESKTOP_UI_DESKTOP_UI_H_

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/animation.h"
#include "alumbra/desktop_ui/compose.h"
#include "alumbra/desktop_ui/draw_list.h"
#include "alumbra/desktop_ui/font_face.h"
#include "alumbra/desktop_ui/gpu_renderer.h"
#include "alumbra/desktop_ui/input_translator.h"
#include "alumbra/desktop_ui/layout.h"
#include "alumbra/desktop_ui/strings.h"
#include "alumbra/desktop_ui/text_shaper.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/machine.h"

namespace alumbra::desktop_ui {

struct DesktopUiOptions {
  design_tokens::Context context;
  design_tokens::Overrides overrides;
  Locale locale = Locale::kEnglish;
  SDL_Window* window = nullptr;
};

class DesktopUi {
 public:
  using EffectSink =
      std::function<void(const std::vector<umbra::ui::SequencedEffect>&)>;

  static absl::StatusOr<std::unique_ptr<DesktopUi>> Create(
      penumbra::desktop::GpuPresenter& presenter, umbra::ui::UiMachine& machine,
      const DesktopUiOptions& options);

  ~DesktopUi();
  DesktopUi(const DesktopUi&) = delete;
  DesktopUi& operator=(const DesktopUi&) = delete;

  void SetEffectSink(EffectSink sink) { sink_ = std::move(sink); }
  bool HandleEvent(const SDL_Event& event);
  bool Frame(std::int64_t now_us);
  bool Frame() { return Frame(machine_.NowMicros()); }

  [[nodiscard]] std::optional<std::int64_t> NextTimerDeadlineUs() const;
  [[nodiscard]] const Layout& GetLayout() const { return layout_; }
  [[nodiscard]] const DrawList& GetDrawList() const { return draw_list_; }
  [[nodiscard]] const design_tokens::Tokens& Tokens() const { return tokens_; }
  void SetTokens(const design_tokens::Context& context,
                 const design_tokens::Overrides& overrides);

 private:
  struct PendingTimer {
    std::int64_t due_us = 0;
    umbra::ui::TimerFired timer;
  };

  DesktopUi(penumbra::desktop::GpuPresenter& presenter,
            umbra::ui::UiMachine& machine, const DesktopUiOptions& options);

  void Dispatch(const std::vector<umbra::ui::Input>& inputs);
  void Apply(const std::vector<umbra::ui::SequencedEffect>& effects);

  penumbra::desktop::GpuPresenter& presenter_;
  umbra::ui::UiMachine& machine_;
  DesktopUiOptions options_;
  design_tokens::Tokens tokens_;
  StringCatalogue catalogue_;
  std::shared_ptr<const FontFace> shared_face_;
  std::unique_ptr<TextShaper> shaper_;
  std::shared_ptr<UiGpuRenderer> renderer_;
  InputTranslator translator_;
  ChromeFade fade_;
  std::shared_ptr<const umbra::ui::ViewModel> snapshot_;
  Layout layout_;
  DrawList draw_list_;
  std::vector<PendingTimer> timers_;
  EffectSink sink_;
  bool animating_ = false;
};

}

#endif
