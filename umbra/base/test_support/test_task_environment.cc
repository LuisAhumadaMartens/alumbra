#include "umbra/base/test_support/test_task_environment.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "absl/time/time.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

TestTaskEnvironment::TestTaskEnvironment() : executor_(clock_) {}

TestTaskEnvironment::~TestTaskEnvironment() { executor_.Shutdown(); }

std::shared_ptr<SequencedTaskRunner>
TestTaskEnvironment::CreateSequencedTaskRunner(TaskPriority priority) {
  return executor_.CreateSequencedTaskRunner(priority);
}

void TestTaskEnvironment::RunUntilIdle() { executor_.RunUntilIdle(); }

void TestTaskEnvironment::FastForwardBy(absl::Duration delta) {
  const TimeTicks target = clock_.NowTicks() + delta;
  for (;;) {
    executor_.RunUntilIdle();
    const std::optional<TimeTicks> next = executor_.NextTaskTime();
    if (!next.has_value() || *next > target) {
      break;
    }
    if (*next > clock_.NowTicks()) {
      clock_.SetNowTicks(*next);
    }
  }
  clock_.SetNowTicks(target);
  executor_.RunUntilIdle();
}

std::optional<absl::Duration> TestTaskEnvironment::NextPendingTaskDelay()
    const {
  const std::optional<TimeTicks> next = executor_.NextTaskTime();
  if (!next.has_value()) {
    return std::nullopt;
  }
  return std::max(*next - clock_.NowTicks(), absl::ZeroDuration());
}

}
