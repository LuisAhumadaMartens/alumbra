#include "alumbra/desktop_ui/animation.h"

#include <algorithm>
#include <cstdint>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/structure.h"
#include "alumbra/desktop_ui/style.h"

namespace alumbra::desktop_ui {
namespace {

float Axis(float first, float second, float t) {
  const float inverse = 1 - t;
  return (3 * inverse * inverse * t * first) + (3 * inverse * t * t * second) +
         (t * t * t);
}

}

float EvaluateEasing(const design_tokens::CubicBezier& curve, float progress) {
  const float x = std::clamp(progress, 0.0F, 1.0F);
  float low = 0;
  float high = 1;
  const auto x1 = static_cast<float>(curve.x1);
  const auto x2 = static_cast<float>(curve.x2);
  while (high - low > kBezierEpsilon) {
    const float middle = (low + high) / 2;
    if (Axis(x1, x2, middle) < x) {
      low = middle;
    } else {
      high = middle;
    }
  }
  const float t = (low + high) / 2;
  return Axis(static_cast<float>(curve.y1), static_cast<float>(curve.y2), t);
}

float SpinnerPhase(std::int64_t now_us, std::int64_t period_us) {
  if (period_us <= 0) {
    return 0;
  }
  const std::int64_t within = now_us % period_us;
  return static_cast<float>(within) / static_cast<float>(period_us);
}

float ChromeFade::Advance(bool visible, std::int64_t now_us,
                          const Style& style) {
  const float goal = visible ? 1.0F : 0.0F;
  if (!initialised_) {
    initialised_ = true;
    target_ = visible;
    value_ = goal;
    from_ = goal;
    start_us_ = now_us;
    settled_ = true;
    return value_;
  }
  if (visible != target_) {
    target_ = visible;
    from_ = value_;
    start_us_ = now_us;
  }
  float progress = 1;
  if (style.fade_us > 0) {
    progress = std::clamp(static_cast<float>(now_us - start_us_) /
                              static_cast<float>(style.fade_us),
                          0.0F, 1.0F);
  }
  settled_ = progress >= 1;
  value_ = from_ + ((goal - from_) * EvaluateEasing(style.easing, progress));
  if (settled_) {
    value_ = goal;
  }
  return value_;
}

}
