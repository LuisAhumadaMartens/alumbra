#include "umbra/base/lock.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>

#include "absl/time/time.h"

namespace umbra::base {

void ConditionVariable::Wait(Lock& lock) {
  std::unique_lock<std::mutex> guard(lock.mutex_, std::adopt_lock);
  condition_.wait(guard);
  guard.release();
}

void ConditionVariable::WaitFor(Lock& lock, absl::Duration timeout) {
  std::unique_lock<std::mutex> guard(lock.mutex_, std::adopt_lock);
  if (timeout > absl::ZeroDuration()) {
    condition_.wait_for(
        guard, absl::ToChronoNanoseconds(std::min(timeout, absl::Hours(24))));
  }
  guard.release();
}

void ConditionVariable::Signal() { condition_.notify_one(); }

void ConditionVariable::Broadcast() { condition_.notify_all(); }

}
