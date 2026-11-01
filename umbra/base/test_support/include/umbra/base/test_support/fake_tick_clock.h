#ifndef UMBRA_BASE_TEST_SUPPORT_FAKE_TICK_CLOCK_H_
#define UMBRA_BASE_TEST_SUPPORT_FAKE_TICK_CLOCK_H_

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

class FakeTickClock final : public TickClock {
 public:
  FakeTickClock() = default;
  ~FakeTickClock() override = default;
  FakeTickClock(const FakeTickClock&) = delete;
  FakeTickClock& operator=(const FakeTickClock&) = delete;

  TimeTicks NowTicks() const override;
  void Advance(absl::Duration delta);
  void SetNowTicks(TimeTicks now);

 private:
  mutable Lock lock_;
  TimeTicks now_ ABSL_GUARDED_BY(lock_);
};

}

#endif
