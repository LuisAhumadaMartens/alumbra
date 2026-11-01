#include "umbra/base/test_support/waitable_event.h"

#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

void WaitableEvent::Signal() {
  AutoLock const lock(lock_);
  signaled_ = true;
  signaled_condition_.Broadcast();
}

bool WaitableEvent::IsSignaled() const {
  AutoLock const lock(lock_);
  return signaled_;
}

void WaitableEvent::Wait() {
  AutoLock const lock(lock_);
  while (!signaled_) {
    signaled_condition_.Wait(lock_);
  }
}

bool WaitableEvent::TimedWait(absl::Duration timeout) {
  const TickClock& clock = GetDefaultTickClock();
  const TimeTicks deadline = clock.NowTicks() + timeout;
  AutoLock const lock(lock_);
  while (!signaled_) {
    const absl::Duration remaining = deadline - clock.NowTicks();
    if (remaining <= absl::ZeroDuration()) {
      break;
    }
    signaled_condition_.WaitFor(lock_, remaining);
  }
  return signaled_;
}

}
