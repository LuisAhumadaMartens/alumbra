#ifndef PENUMBRA_DESKTOP_VIDEO_PRESENTATION_TIMING_H_
#define PENUMBRA_DESKTOP_VIDEO_PRESENTATION_TIMING_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "penumbra/interface/rational.h"

namespace penumbra::desktop {

enum class DisplayRateMatch { kOff, kSeamless, kAlways };

enum class DisplayRateMatchStatus : uint8_t {
  kOff,
  kUnknownContentRate,
  kAlreadyMatched,
  kRequestPending,
  kUnsupported,
  kNoExactMultiple,
};

struct DisplayRateMatchPlan {
  DisplayRateMatchStatus status = DisplayRateMatchStatus::kUnknownContentRate;
  std::optional<Rational> requested_rate;
  std::string reason;
};

struct PlatformTimingState {
  bool available = false;
  std::string fallback_reason;
};

[[nodiscard]] DisplayRateMatchPlan PlanDisplayRateMatch(
    DisplayRateMatch mode, Rational content_rate, Rational current_rate,
    const std::vector<Rational>& available_rates, bool seamless_supported,
    bool mode_switch_supported);

[[nodiscard]] PlatformTimingState DescribePlatformTiming(
    bool offscreen, const std::string& platform, const std::string& driver);

}

#endif
