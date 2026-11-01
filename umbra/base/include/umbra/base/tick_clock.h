#ifndef UMBRA_BASE_TICK_CLOCK_H_
#define UMBRA_BASE_TICK_CLOCK_H_

#include "umbra/base/time_ticks.h"

namespace umbra::base {

class TickClock {
 public:
  virtual ~TickClock() = default;

  virtual TimeTicks NowTicks() const = 0;
};

const TickClock& GetDefaultTickClock();

}

#endif
