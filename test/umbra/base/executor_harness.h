#ifndef TEST_UMBRA_BASE_EXECUTOR_HARNESS_H_
#define TEST_UMBRA_BASE_EXECUTOR_HARNESS_H_

#include <memory>
#include <ostream>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/thread_pool.h"

namespace umbra::base {

enum class ExecutorKind {
  kThreadPool,
  kSingleThread,
};

std::string ExecutorKindName(ExecutorKind kind);
void PrintTo(ExecutorKind kind, std::ostream* stream);

class ExecutorHarness {
 public:
  explicit ExecutorHarness(ExecutorKind kind);
  ExecutorHarness(const ExecutorHarness&) = delete;
  ExecutorHarness& operator=(const ExecutorHarness&) = delete;
  ~ExecutorHarness();

  TaskExecutor& executor();
  bool RunUntil(WaitableEvent& done,
                absl::Duration timeout = absl::Seconds(60));
  void Shutdown();

 private:
  void WakeUp();

  const ExecutorKind kind_;
  Lock wake_lock_;
  ConditionVariable wake_condition_;
  bool woken_ ABSL_GUARDED_BY(wake_lock_) = false;
  std::unique_ptr<ThreadPool> pool_;
  std::unique_ptr<SingleThreadTaskExecutor> single_thread_;
};

}

#endif
