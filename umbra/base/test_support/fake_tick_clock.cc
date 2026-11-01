#include "umbra/base/test_support/fake_tick_clock.h"

#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

TimeTicks FakeTickClock::NowTicks() const {
  AutoLock lock(lock_);
  return now_;
}

void FakeTickClock::Advance(absl::Duration delta) {
  AutoLock lock(lock_);
  now_ = now_ + delta;
}

void FakeTickClock::SetNowTicks(TimeTicks now) {
  AutoLock lock(lock_);
  now_ = now;
}

}
