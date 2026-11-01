#ifndef UMBRA_BASE_TEST_SUPPORT_WAITABLE_EVENT_H_
#define UMBRA_BASE_TEST_SUPPORT_WAITABLE_EVENT_H_

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "umbra/base/lock.h"

namespace umbra::base {

class WaitableEvent {
 public:
  WaitableEvent() = default;
  WaitableEvent(const WaitableEvent&) = delete;
  WaitableEvent& operator=(const WaitableEvent&) = delete;

  void Signal();
  bool IsSignaled() const;
  void Wait();
  bool TimedWait(absl::Duration timeout);

 private:
  mutable Lock lock_;
  ConditionVariable signaled_condition_;
  bool signaled_ ABSL_GUARDED_BY(lock_) = false;
};

}

#endif
