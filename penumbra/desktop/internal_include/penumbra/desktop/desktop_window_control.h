#ifndef PENUMBRA_DESKTOP_DESKTOP_WINDOW_CONTROL_H_
#define PENUMBRA_DESKTOP_DESKTOP_WINDOW_CONTROL_H_

#include <SDL3/SDL_video.h>

#include <functional>

namespace penumbra::desktop {

class GpuPresenter;

class DesktopWindowControl {
 public:
  virtual ~DesktopWindowControl() = default;
  virtual void AttachWindow(
      SDL_Window* window, std::function<void(GpuPresenter&)> on_presenter) = 0;
};

}

#endif
