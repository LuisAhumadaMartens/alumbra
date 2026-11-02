#ifndef ALUMBRA_DESKTOP_UI_ICONS_H_
#define ALUMBRA_DESKTOP_UI_ICONS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "alumbra/desktop_ui/structure.h"

namespace alumbra::desktop_ui {

enum class IconId : std::uint8_t {
  kPlay,
  kPause,
  kVolumeOn,
  kVolumeOff,
  kFullscreen,
  kFullscreenExit,
  kSettings,
  kSkipAd,
  kError,
  kDot,
};

inline constexpr std::size_t kIconCount = 10;

[[nodiscard]] IconId IconFromToken(std::string_view token);

struct IconAtlasCell {
  float u0 = 0;
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
};

class IconSdfAtlas {
 public:
  IconSdfAtlas();

  [[nodiscard]] static int Size() { return kIconAtlasSize; }
  [[nodiscard]] const std::vector<std::uint8_t>& Pixels() const {
    return pixels_;
  }
  [[nodiscard]] static IconAtlasCell Cell(IconId icon);
  [[nodiscard]] static float CellScale();

 private:
  std::vector<std::uint8_t> pixels_;
};

}

#endif
