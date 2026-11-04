#include "alumbra/desktop_ui/desktop_window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "alumbra/desktop_ui/desktop_ui.h"
#include "penumbra/desktop/desktop_port.h"
#include "penumbra/desktop/desktop_window_control.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/sdl_session.h"
#include "penumbra/interface/port.h"
#include "umbra/base/tick_clock.h"
#include "umbra/player/events.h"
#include "umbra/player/port_access.h"
#include "umbra/player/time_ranges.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/machine.h"
#include "umbra/ui/types.h"

namespace alumbra::desktop_ui {
namespace {

std::int64_t ToMicros(double seconds) {
  if (!std::isfinite(seconds)) {
    return 0;
  }
  return static_cast<std::int64_t>(std::llround(seconds * 1'000'000.0));
}

void DispatchUi(umbra::ui::UiMachine& machine, umbra::ui::EngineEvent event) {
  static_cast<void>(machine.Dispatch(umbra::ui::Input{std::move(event)}));
}

}

class DesktopWindow::Impl {
 public:
  Impl(umbra::player::Player& player,
       std::unique_ptr<penumbra::desktop::SdlVideoSession> session,
       std::unique_ptr<penumbra::desktop::SdlWindow> window)
      : player_(player),
        session_(std::move(session)),
        window_(std::move(window)),
        machine_(umbra::base::GetDefaultTickClock(), {}) {}

  void Attach(penumbra::desktop::DesktopWindowControl& control) {
    control.AttachWindow(window_->get(), [this](auto& presenter) {
      presenter_ = &presenter;
      InitialiseUi();
    });
  }

  bool PumpEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return false;
      }
      if (presenter_ != nullptr) {
        static_cast<void>(presenter_->HandleEvent(event));
      }
      if (ui_ != nullptr) {
        static_cast<void>(ui_->HandleEvent(event));
      }
    }
    if (SDL_WaitEventTimeout(&event, 8)) {
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return false;
      }
      if (presenter_ != nullptr) {
        static_cast<void>(presenter_->HandleEvent(event));
      }
      if (ui_ != nullptr) {
        static_cast<void>(ui_->HandleEvent(event));
      }
    }
    return true;
  }

  bool RenderFrame() { return ui_ != nullptr && ui_->Frame(); }

 private:
  void InitialiseUi() {
    DispatchUi(machine_, umbra::ui::LoadStarted{});
    UpdateState();
    DesktopUiOptions options;
    options.window = window_->get();
    absl::StatusOr<std::unique_ptr<DesktopUi>> created =
        DesktopUi::Create(*presenter_, machine_, options);
    if (!created.ok()) {
      return;
    }
    ui_ = std::move(*created);
    ui_->SetEffectSink([this](const auto& effects) {
      for (const auto& sequenced : effects) {
        if (std::holds_alternative<umbra::ui::PlayerPlay>(sequenced.effect)) {
          player_.Play([](const auto&) {});
        } else if (std::holds_alternative<umbra::ui::PlayerPause>(
                       sequenced.effect)) {
          player_.Pause();
        } else if (const auto* seek =
                       std::get_if<umbra::ui::PlayerSeek>(&sequenced.effect)) {
          static_cast<void>(player_.set_current_time(
              static_cast<double>(seek->position_us) / 1'000'000.0));
        } else if (const auto* volume = std::get_if<umbra::ui::PlayerSetVolume>(
                       &sequenced.effect)) {
          static_cast<void>(player_.set_volume(volume->volume));
        } else if (const auto* muted = std::get_if<umbra::ui::PlayerSetMuted>(
                       &sequenced.effect)) {
          player_.set_muted(muted->muted);
        }
      }
    });
    AddListeners();
  }

  void UpdateState() {
    const double duration = player_.duration();
    DispatchUi(machine_,
               umbra::ui::DurationChanged{.duration_us = ToMicros(duration),
                                          .live = std::isinf(duration)});
    const umbra::player::TimeRanges seekable = player_.seekable();
    if (seekable.length() > 0U) {
      DispatchUi(machine_,
                 umbra::ui::SeekableChanged{
                     .start_us = ToMicros(seekable.start(0)),
                     .end_us = ToMicros(seekable.end(seekable.length() - 1U))});
    }
    const umbra::player::TimeRanges buffered = player_.buffered();
    std::vector<umbra::ui::TimeRange> ranges;
    ranges.reserve(buffered.length());
    for (std::size_t index = 0; index < buffered.length(); ++index) {
      ranges.push_back({.start_us = ToMicros(buffered.start(index)),
                        .end_us = ToMicros(buffered.end(index))});
    }
    DispatchUi(machine_, umbra::ui::BufferedChanged{std::move(ranges)});
    DispatchUi(machine_,
               umbra::ui::VolumeChanged{player_.volume(), player_.muted()});
  }

  void AddListeners() {
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::CanPlayEvent>(
            [this](const auto&) {
              DispatchUi(machine_, umbra::ui::CanPlay{});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::PlayingEvent>(
            [this](const auto&) {
              DispatchUi(machine_, umbra::ui::PlaybackStarted{});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::PauseEvent>(
            [this](const auto&) {
              DispatchUi(machine_, umbra::ui::PlaybackPaused{});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::WaitingEvent>(
            [this](const auto&) {
              DispatchUi(machine_, umbra::ui::Waiting{});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::TimeUpdateEvent>(
            [this](const auto& event) {
              UpdateState();
              DispatchUi(machine_,
                         umbra::ui::TimeUpdate{
                             .position_us = ToMicros(event.state.current_time),
                             .anchor_us = machine_.NowMicros()});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::DurationChangeEvent>(
            [this](const auto& event) {
              DispatchUi(machine_,
                         umbra::ui::DurationChanged{
                             .duration_us = ToMicros(event.state.duration),
                             .live = std::isinf(event.state.duration)});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::EndedEvent>(
            [this](const auto&) {
              DispatchUi(machine_, umbra::ui::PlaybackEnded{});
            }));
    subscriptions_.push_back(
        player_.AddEventListener<umbra::player::VolumeChangeEvent>(
            [this](const auto&) {
              DispatchUi(machine_,
                         umbra::ui::VolumeChanged{.volume = player_.volume(),
                                                  .muted = player_.muted()});
            }));
  }

  umbra::player::Player& player_;
  std::unique_ptr<penumbra::desktop::SdlVideoSession> session_;
  std::unique_ptr<penumbra::desktop::SdlWindow> window_;
  penumbra::desktop::GpuPresenter* presenter_ = nullptr;
  umbra::ui::UiMachine machine_;
  std::unique_ptr<DesktopUi> ui_;
  std::vector<umbra::player::Subscription> subscriptions_;
};

std::unique_ptr<DesktopWindow> DesktopWindow::Create(
    umbra::player::Player& player, const DesktopWindowOptions& options) {
  if (options.width <= 0 || options.height <= 0) {
    return nullptr;
  }
  auto session = penumbra::desktop::SdlVideoSession::Create(
      penumbra::desktop::SessionMode::kWindowed);
  if (!session.ok()) {
    return nullptr;
  }
  auto window = penumbra::desktop::SdlWindow::Create(
      **session, options.title,
      {.width = options.width, .height = options.height});
  if (!window.ok()) {
    return nullptr;
  }
  if (options.fullscreen && !SDL_SetWindowFullscreen((*window)->get(), true)) {
    return nullptr;
  }
  auto impl =
      std::make_unique<Impl>(player, std::move(*session), std::move(*window));
  const std::shared_ptr<penumbra::Port> port =
      umbra::player::internal::GetPort(player);
  auto* control = port == nullptr
                      ? nullptr
                      : penumbra::desktop::GetDesktopWindowControl(*port);
  if (control == nullptr) {
    return nullptr;
  }
  impl->Attach(*control);
  return std::unique_ptr<DesktopWindow>(new DesktopWindow(std::move(impl)));
}

DesktopWindow::DesktopWindow(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

DesktopWindow::~DesktopWindow() = default;

bool DesktopWindow::PumpEvents() { return impl_->PumpEvents(); }

bool DesktopWindow::RenderFrame() { return impl_->RenderFrame(); }

}
