#include "executor_harness.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/thread_pool.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

std::string ExecutorKindName(ExecutorKind kind) {
  return kind == ExecutorKind::kThreadPool ? "ThreadPool" : "SingleThread";
}

void PrintTo(ExecutorKind kind, std::ostream* stream) {
  *stream << ExecutorKindName(kind);
}

ExecutorHarness::ExecutorHarness(ExecutorKind kind) : kind_(kind) {
  if (kind_ == ExecutorKind::kThreadPool) {
    pool_ = std::make_unique<ThreadPool>(ThreadPoolOptions{.max_workers = 4});
  } else {
    single_thread_ = std::make_unique<SingleThreadTaskExecutor>(
        GetDefaultTickClock(), [this] { WakeUp(); });
  }
}

ExecutorHarness::~ExecutorHarness() { Shutdown(); }

TaskExecutor& ExecutorHarness::executor() {
  if (pool_ != nullptr) {
    return *pool_;
  }
  return *single_thread_;
}

bool ExecutorHarness::RunUntil(WaitableEvent& done, absl::Duration timeout) {
  if (pool_ != nullptr) {
    return done.TimedWait(timeout);
  }
  const TickClock& clock = GetDefaultTickClock();
  const TimeTicks deadline = clock.NowTicks() + timeout;
  while (!done.IsSignaled()) {
    single_thread_->RunUntilIdle();
    if (done.IsSignaled()) {
      break;
    }
    const TimeTicks now = clock.NowTicks();
    if (now >= deadline) {
      return false;
    }
    absl::Duration wait = deadline - now;
    const std::optional<TimeTicks> next = single_thread_->NextTaskTime();
    if (next.has_value()) {
      wait = std::min(wait, *next - now);
    }
    AutoLock const lock(wake_lock_);
    if (!woken_) {
      wake_condition_.WaitFor(wake_lock_, wait);
    }
    woken_ = false;
  }
  return true;
}

void ExecutorHarness::Shutdown() {
  if (pool_ != nullptr) {
    pool_->Shutdown();
  } else {
    single_thread_->Shutdown();
  }
}

void ExecutorHarness::WakeUp() {
  AutoLock const lock(wake_lock_);
  woken_ = true;
  wake_condition_.Signal();
}

}
