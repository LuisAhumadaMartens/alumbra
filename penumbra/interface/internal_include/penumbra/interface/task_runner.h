#ifndef PENUMBRA_INTERFACE_TASK_RUNNER_H_
#define PENUMBRA_INTERFACE_TASK_RUNNER_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace penumbra {

enum class TaskPriority { kBackground, kControl };
enum class BlockingPolicy { kNonBlocking, kMayBlock };

class ControlTaskRunner {
 public:
  using Task = absl::AnyInvocable<void() &&>;
  virtual ~ControlTaskRunner() = default;
  virtual absl::Status PostTask(Task task, absl::Duration delay) = 0;
  virtual bool RunsTasksInCurrentSequence() const = 0;
};

class TaskRunnerProvider {
 public:
  virtual ~TaskRunnerProvider() = default;
  virtual absl::StatusOr<std::shared_ptr<ControlTaskRunner>> CreateTaskRunner(
      TaskPriority priority, BlockingPolicy blocking) = 0;
};

}

#endif
