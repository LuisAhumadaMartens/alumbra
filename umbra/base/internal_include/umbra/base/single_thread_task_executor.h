#ifndef UMBRA_BASE_SINGLE_THREAD_TASK_EXECUTOR_H_
#define UMBRA_BASE_SINGLE_THREAD_TASK_EXECUTOR_H_

#include <memory>
#include <cstdint>
#include <optional>

#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {

class SingleThreadTaskExecutor final : public TaskExecutor {
 public:
  using WakeUpCallback = absl::AnyInvocable<void() const>;
  enum class Ordering : uint8_t { kPriority, kChronological };

  explicit SingleThreadTaskExecutor(
      const TickClock& clock = GetDefaultTickClock(),
      WakeUpCallback wake_up = nullptr,
      Ordering ordering = Ordering::kPriority);
  ~SingleThreadTaskExecutor() override;
  SingleThreadTaskExecutor(const SingleThreadTaskExecutor&) = delete;
  SingleThreadTaskExecutor& operator=(const SingleThreadTaskExecutor&) = delete;

  std::shared_ptr<SequencedTaskRunner> CreateSequencedTaskRunner(
      TaskPriority priority) override;

  void RunUntilIdle();
  bool RunFor(absl::Duration budget);
  [[nodiscard]] std::optional<TimeTicks> NextTaskTime() const;
  void Shutdown();

 private:
  class Core;
  class Sequence;

  std::shared_ptr<Core> core_;
};

}

#endif
