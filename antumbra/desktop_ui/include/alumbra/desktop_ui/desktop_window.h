#ifndef ALUMBRA_DESKTOP_UI_DESKTOP_WINDOW_H_
#define ALUMBRA_DESKTOP_UI_DESKTOP_WINDOW_H_

#include <cstdint>
#include <memory>
#include <string>

#include "umbra/player/player.h"

namespace alumbra::desktop_ui {

struct DesktopWindowOptions {
  std::string title = "Alumbra";
  std::int32_t width = 1280;
  std::int32_t height = 720;
  bool fullscreen = false;
};

class DesktopWindow {
 public:
  [[nodiscard]] static std::unique_ptr<DesktopWindow> Create(
      umbra::player::Player& player, const DesktopWindowOptions& options = {});

  DesktopWindow(const DesktopWindow&) = delete;
  DesktopWindow& operator=(const DesktopWindow&) = delete;
  ~DesktopWindow();

  [[nodiscard]] bool PumpEvents();
  [[nodiscard]] bool RenderFrame();

 private:
  class Impl;
  explicit DesktopWindow(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}

#endif
