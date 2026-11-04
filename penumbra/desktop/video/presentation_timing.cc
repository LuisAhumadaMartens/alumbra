#include "penumbra/desktop/video/presentation_timing.h"

#include <numeric>
#include <string>
#include <vector>

#include "penumbra/interface/rational.h"

namespace penumbra::desktop {
namespace {

bool IsExactMultiple(Rational display_rate, Rational content_rate) {
  if (!display_rate.is_positive() || !content_rate.is_positive()) {
    return false;
  }
  int64_t display_numerator = display_rate.numerator();
  int64_t content_denominator = content_rate.denominator();
  int64_t display_denominator = display_rate.denominator();
  int64_t content_numerator = content_rate.numerator();
  const int64_t first = std::gcd(display_numerator, content_numerator);
  display_numerator /= first;
  content_numerator /= first;
  const int64_t second = std::gcd(content_denominator, display_denominator);
  content_denominator /= second;
  display_denominator /= second;
  return content_numerator == 1 && display_denominator == 1;
}

}

DisplayRateMatchPlan PlanDisplayRateMatch(
    DisplayRateMatch mode, Rational content_rate, Rational current_rate,
    const std::vector<Rational>& available_rates, bool seamless_supported,
    bool mode_switch_supported) {
  if (mode == DisplayRateMatch::kOff) {
    return {.status = DisplayRateMatchStatus::kOff,
            .requested_rate = std::nullopt,
            .reason = "Display rate matching is disabled"};
  }
  if (!content_rate.is_positive()) {
    return {.status = DisplayRateMatchStatus::kUnknownContentRate,
            .requested_rate = std::nullopt,
            .reason = "The content frame rate is unknown"};
  }
  if (IsExactMultiple(current_rate, content_rate)) {
    return {.status = DisplayRateMatchStatus::kAlreadyMatched,
            .requested_rate = current_rate,
            .reason = {}};
  }
  const bool can_change_rate =
      seamless_supported ||
      (mode == DisplayRateMatch::kAlways && mode_switch_supported);
  if (!can_change_rate) {
    return {.status = DisplayRateMatchStatus::kUnsupported,
            .requested_rate = std::nullopt,
            .reason =
                "The platform cannot make the configured display-rate change"};
  }
  for (const Rational candidate : available_rates) {
    if (IsExactMultiple(candidate, content_rate)) {
      return {.status = DisplayRateMatchStatus::kRequestPending,
              .requested_rate = candidate,
              .reason = {}};
    }
  }
  return {.status = DisplayRateMatchStatus::kNoExactMultiple,
          .requested_rate = std::nullopt,
          .reason = "No available display rate is an exact content multiple"};
}

PlatformTimingState DescribePlatformTiming(bool offscreen,
                                           const std::string& platform,
                                           const std::string& driver) {
  if (offscreen) {
    return {.available = false,
            .fallback_reason =
                "Offscreen presentation has no display timing source"};
  }
  if (platform == "macOS") {
    return {.available = false,
            .fallback_reason =
                "SDL_GPU does not expose its CAMetalLayer or MTLDrawable for "
                "CAMetalDisplayLink deadlines and presented handlers"};
  }
  if (driver == "wayland") {
    return {.available = false,
            .fallback_reason =
                "SDL_GPU does not bind the Wayland presentation-time protocol "
                "for its swapchain surface"};
  }
  if (driver == "x11") {
    return {.available = false,
            .fallback_reason =
                "SDL_GPU does not expose X11 Present completion events for its "
                "swapchain window"};
  }
  return {.available = false,
          .fallback_reason =
              "No platform presentation-time source is available for this "
              "SDL video driver"};
}

}
