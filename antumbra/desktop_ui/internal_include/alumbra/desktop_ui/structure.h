#ifndef ALUMBRA_DESKTOP_UI_STRUCTURE_H_
#define ALUMBRA_DESKTOP_UI_STRUCTURE_H_

#include <cstddef>
#include <cstdint>
#include <numbers>

namespace alumbra::desktop_ui {

inline constexpr int kIconAtlasSize = 256;
inline constexpr int kIconCellSize = 64;
inline constexpr int kIconCellsPerRow = 4;
inline constexpr int kIconGlyphSize = 48;
inline constexpr float kIconSdfSpread = 8.0F;
inline constexpr float kIconGridUnits = 24.0F;

inline constexpr int kGlyphAtlasSize = 1024;
inline constexpr int kGlyphPadding = 1;

inline constexpr std::size_t kSpinnerDots = 8;

inline constexpr std::int64_t kMicrosecondsPerHour = 3'600'000'000;

inline constexpr float kBezierEpsilon = 0.0001F;

inline constexpr float kPi = std::numbers::pi_v<float>;

}

#endif
