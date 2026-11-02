#ifndef ALUMBRA_DESKTOP_UI_ANIMATION_H_
#define ALUMBRA_DESKTOP_UI_ANIMATION_H_

#include <cstdint>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/style.h"

namespace alumbra::desktop_ui {

[[nodiscard]] float EvaluateEasing(const design_tokens::CubicBezier& curve,
                                   float progress);
[[nodiscard]] float SpinnerPhase(std::int64_t now_us, std::int64_t period_us);

class ChromeFade {
 public:
  [[nodiscard]] float Advance(bool visible, std::int64_t now_us,
                              const Style& style);
  [[nodiscard]] bool Settled() const { return settled_; }

 private:
  bool initialised_ = false;
  bool target_ = false;
  bool settled_ = true;
  float from_ = 0;
  float value_ = 0;
  std::int64_t start_us_ = 0;
};

}

#endif
