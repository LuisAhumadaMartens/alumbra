#include "penumbra/desktop/video/sdl_session.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/desktop/video/video_geometry.h"

namespace penumbra::desktop {

absl::StatusOr<std::unique_ptr<SdlVideoSession>> SdlVideoSession::Create(
    SessionMode mode) {
  const std::string platform = SDL_GetPlatform();
  if (mode == SessionMode::kHeadless && platform != "macOS") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  }
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    return absl::UnavailableError(
        absl::StrCat("SDL_InitSubSystem(video): ", SDL_GetError()));
  }
  return std::unique_ptr<SdlVideoSession>(new SdlVideoSession());
}

SdlVideoSession::~SdlVideoSession() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }

absl::StatusOr<std::unique_ptr<SdlWindow>> SdlWindow::Create(
    const SdlVideoSession& session, const std::string& title,
    PixelSize logical_size) {
  static_cast<void>(session);
  SDL_Window* window =
      SDL_CreateWindow(title.c_str(), logical_size.width, logical_size.height,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    return absl::UnavailableError(
        absl::StrCat("SDL_CreateWindow: ", SDL_GetError()));
  }
  return std::unique_ptr<SdlWindow>(new SdlWindow(window));
}

SdlWindow::~SdlWindow() { SDL_DestroyWindow(window_); }

}
