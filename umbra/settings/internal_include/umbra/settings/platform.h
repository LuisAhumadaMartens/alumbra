#ifndef UMBRA_SETTINGS_PLATFORM_H_
#define UMBRA_SETTINGS_PLATFORM_H_

#include <cstddef>
#include <cstdint>

namespace umbra::settings {

enum class Platform : std::uint8_t {
  kLinux,
  kMacos,
  kWindows,
  kAndroid,
  kGoogleTv,
  kIos,
  kIpados,
  kTvos,
  kWeb,
};

inline constexpr std::size_t kPlatformCount = 9;

}

#endif
