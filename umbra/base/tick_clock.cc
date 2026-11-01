#include "umbra/base/tick_clock.h"

#include <chrono>

#include "absl/time/time.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {
namespace {

class SteadyTickClock final : public TickClock {
 public:
  TimeTicks NowTicks() const override {
    const auto since_epoch =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    return TimeTicks::FromSinceOrigin(absl::FromChrono(since_epoch));
  }
};

}

const TickClock& GetDefaultTickClock() {
  static const SteadyTickClock* const clock = new SteadyTickClock;
  return *clock;
}

}
