#ifndef UMBRA_BASE_TEST_SUPPORT_TEST_TASK_ENVIRONMENT_H_
#define UMBRA_BASE_TEST_SUPPORT_TEST_TASK_ENVIRONMENT_H_

#include <memory>
#include <optional>

#include "absl/time/time.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/fake_tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

class TestTaskEnvironment final : public TaskExecutor {
 public:
  TestTaskEnvironment();
  ~TestTaskEnvironment() override;
  TestTaskEnvironment(const TestTaskEnvironment&) = delete;
  TestTaskEnvironment& operator=(const TestTaskEnvironment&) = delete;

  std::shared_ptr<SequencedTaskRunner> CreateSequencedTaskRunner(
      TaskPriority priority) override;

  FakeTickClock& clock() { return clock_; }
  SingleThreadTaskExecutor& executor() { return executor_; }
  TimeTicks NowTicks() const { return clock_.NowTicks(); }

  void RunUntilIdle();
  void FastForwardBy(absl::Duration delta);
  std::optional<absl::Duration> NextPendingTaskDelay() const;

 private:
  FakeTickClock clock_;
  SingleThreadTaskExecutor executor_;
};

}

#endif
