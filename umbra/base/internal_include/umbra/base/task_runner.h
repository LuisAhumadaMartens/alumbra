#ifndef UMBRA_BASE_TASK_RUNNER_H_
#define UMBRA_BASE_TASK_RUNNER_H_

#include <memory>
#include <cstdint>
#include <source_location>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"

namespace umbra::base {

enum class TaskPriority : uint8_t {
  kBackground,
  kControl,
};

class TaskRunner {
 public:
  using Task = absl::AnyInvocable<void() &&>;

  TaskRunner(const TaskRunner&) = delete;
  TaskRunner& operator=(const TaskRunner&) = delete;
  virtual ~TaskRunner() = default;

  bool PostTask(Task task, std::source_location posted_from =
                               std::source_location::current()) {
    return DoPostTask(std::move(task), absl::ZeroDuration(), posted_from);
  }

  bool PostDelayedTask(
      Task task, absl::Duration delay,
      std::source_location posted_from = std::source_location::current()) {
    return DoPostTask(std::move(task), delay, posted_from);
  }

  [[nodiscard]] virtual bool RunsTasksInCurrentSequence() const = 0;

 protected:
  TaskRunner() = default;

  virtual bool DoPostTask(Task task, absl::Duration delay,
                          std::source_location posted_from) = 0;
};

class SequencedTaskRunner : public TaskRunner {
 protected:
  SequencedTaskRunner() = default;
};

class TaskExecutor {
 public:
  TaskExecutor(const TaskExecutor&) = delete;
  TaskExecutor& operator=(const TaskExecutor&) = delete;
  virtual ~TaskExecutor() = default;

  virtual std::shared_ptr<SequencedTaskRunner> CreateSequencedTaskRunner(
      TaskPriority priority) = 0;

 protected:
  TaskExecutor() = default;
};

}

#endif
