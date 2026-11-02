#ifndef TEST_PENUMBRA_CONFORMANCE_CONTROL_RUNNER_ADAPTER_H_
#define TEST_PENUMBRA_CONFORMANCE_CONTROL_RUNNER_ADAPTER_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/task_runner.h"

namespace penumbra::conformance {

class ControlRunnerAdapter final : public ControlTaskRunner {
 public:
  explicit ControlRunnerAdapter(
      std::shared_ptr<umbra::base::SequencedTaskRunner> runner);
  absl::Status PostTask(Task task, absl::Duration delay) override;
  [[nodiscard]] bool RunsTasksInCurrentSequence() const override;

 private:
  std::shared_ptr<umbra::base::SequencedTaskRunner> runner_;
};

}

#endif
