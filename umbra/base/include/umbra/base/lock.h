#ifndef UMBRA_BASE_LOCK_H_
#define UMBRA_BASE_LOCK_H_

#include <condition_variable>
#include <mutex>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"

namespace umbra::base {

class ABSL_LOCKABLE Lock {
 public:
  Lock() = default;
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

  void Acquire() ABSL_EXCLUSIVE_LOCK_FUNCTION() { mutex_.lock(); }
  void Release() ABSL_UNLOCK_FUNCTION() { mutex_.unlock(); }

 private:
  friend class ConditionVariable;

  std::mutex mutex_;
};

class ABSL_SCOPED_LOCKABLE AutoLock {
 public:
  explicit AutoLock(Lock& lock) ABSL_EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_.Acquire();
  }
  AutoLock(const AutoLock&) = delete;
  AutoLock& operator=(const AutoLock&) = delete;
  ~AutoLock() ABSL_UNLOCK_FUNCTION() { lock_.Release(); }

 private:
  Lock& lock_;
};

class ConditionVariable {
 public:
  ConditionVariable() = default;
  ConditionVariable(const ConditionVariable&) = delete;
  ConditionVariable& operator=(const ConditionVariable&) = delete;

  void Wait(Lock& lock) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock);
  void WaitFor(Lock& lock, absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock);
  void Signal();
  void Broadcast();

 private:
  std::condition_variable condition_;
};

}

#endif
