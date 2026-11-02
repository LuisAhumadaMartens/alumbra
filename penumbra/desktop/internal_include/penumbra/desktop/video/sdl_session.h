#ifndef PENUMBRA_DESKTOP_VIDEO_SDL_SESSION_H_
#define PENUMBRA_DESKTOP_VIDEO_SDL_SESSION_H_

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "penumbra/desktop/video/video_geometry.h"

namespace penumbra::desktop {

enum class SessionMode {
  kWindowed,
  kHeadless,
};

class SdlVideoSession {
 public:
  static absl::StatusOr<std::unique_ptr<SdlVideoSession>> Create(
      SessionMode mode);

  ~SdlVideoSession();
  SdlVideoSession(const SdlVideoSession&) = delete;
  SdlVideoSession& operator=(const SdlVideoSession&) = delete;

 private:
  SdlVideoSession() = default;
};

class SdlWindow {
 public:
  static absl::StatusOr<std::unique_ptr<SdlWindow>> Create(
      const SdlVideoSession& session, const std::string& title,
      PixelSize logical_size);

  ~SdlWindow();
  SdlWindow(const SdlWindow&) = delete;
  SdlWindow& operator=(const SdlWindow&) = delete;

  [[nodiscard]] SDL_Window* get() const { return window_; }

 private:
  explicit SdlWindow(SDL_Window* window) : window_(window) {}

  SDL_Window* window_;
};

}

#endif
