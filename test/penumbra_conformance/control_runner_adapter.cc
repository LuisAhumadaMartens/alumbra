#include "penumbra_conformance/control_runner_adapter.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "umbra/base/task_runner.h"

namespace penumbra::conformance {

ControlRunnerAdapter::ControlRunnerAdapter(
    std::shared_ptr<umbra::base::SequencedTaskRunner> runner)
    : runner_(std::move(runner)) {}

absl::Status ControlRunnerAdapter::PostTask(Task task, absl::Duration delay) {
  if (!task || delay < absl::ZeroDuration() ||
      delay == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("invalid task or delay");
  }
  if (!runner_->PostDelayedTask(std::move(task), delay)) {
    return absl::UnavailableError("task runner is shut down");
  }
  return absl::OkStatus();
}

bool ControlRunnerAdapter::RunsTasksInCurrentSequence() const {
  return runner_->RunsTasksInCurrentSequence();
}

}
