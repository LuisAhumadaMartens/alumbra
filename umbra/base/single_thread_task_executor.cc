#include "umbra/base/single_thread_task_executor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/sequence_token.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {
namespace {

struct ScheduledTask {
  TimeTicks due;
  std::uint64_t order = 0;
  SequenceToken sequence;
  std::source_location posted_from;
  TaskRunner::Task task;
};

bool RunsLater(const ScheduledTask& lhs, const ScheduledTask& rhs) {
  if (lhs.due != rhs.due) {
    return lhs.due > rhs.due;
  }
  return lhs.order > rhs.order;
}

}

class SingleThreadTaskExecutor::Core {
 public:
  Core(const TickClock& clock, WakeUpCallback wake_up, Ordering ordering)
      : clock_(clock), wake_up_(std::move(wake_up)), ordering_(ordering) {}

  const TickClock& clock() const { return clock_; }

  bool Post(SequenceToken sequence, TaskPriority priority,
            TaskRunner::Task task, absl::Duration delay,
            std::source_location posted_from) {
    const std::lock_guard wake_lock(wake_lock_);
    bool wake = false;
    {
      AutoLock lock(lock_);
      if (shut_down_) {
        return false;
      }
      const TimeTicks due =
          clock_.NowTicks() + std::max(delay, absl::ZeroDuration());
      const std::optional<TimeTicks> previous = NextTaskTimeLocked();
      std::vector<ScheduledTask>& queue = QueueFor(priority);
      queue.push_back(ScheduledTask{.due = due,
                                    .order = next_order_++,
                                    .sequence = sequence,
                                    .posted_from = posted_from,
                                    .task = std::move(task)});
      std::push_heap(queue.begin(), queue.end(), RunsLater);
      wake = !previous.has_value() || due < *previous;
    }
    if (wake && wake_up_) {
      wake_up_();
    }
    return true;
  }

  bool RunNextDueTask() {
    ScheduledTask next;
    {
      AutoLock lock(lock_);
      std::vector<ScheduledTask>* queue = DueQueueLocked();
      if (queue == nullptr) {
        return false;
      }
      std::pop_heap(queue->begin(), queue->end(), RunsLater);
      next = std::move(queue->back());
      queue->pop_back();
    }
    ScopedTaskContext context(next.sequence, next.posted_from);
    std::move(next.task)();
    next.task = nullptr;
    return true;
  }

  bool HasDueTask() {
    AutoLock lock(lock_);
    return DueQueueLocked() != nullptr;
  }

  std::optional<TimeTicks> NextTaskTime() {
    AutoLock lock(lock_);
    return NextTaskTimeLocked();
  }

  void Shutdown() {
    std::vector<ScheduledTask> control;
    std::vector<ScheduledTask> background;
    {
      const std::lock_guard wake_lock(wake_lock_);
      AutoLock lock(lock_);
      shut_down_ = true;
      control.swap(control_);
      background.swap(background_);
    }
  }

 private:
  std::vector<ScheduledTask>& QueueFor(TaskPriority priority)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return priority == TaskPriority::kControl ? control_ : background_;
  }

  std::vector<ScheduledTask>* DueQueueLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    if (shut_down_) {
      return nullptr;
    }
    const TimeTicks now = clock_.NowTicks();
    if (ordering_ == Ordering::kChronological && !background_.empty() &&
        background_.front().due <= now &&
        (control_.empty() ||
         RunsLater(control_.front(), background_.front()))) {
      return &background_;
    }
    if (!control_.empty() && control_.front().due <= now) {
      return &control_;
    }
    if (!background_.empty() && background_.front().due <= now) {
      return &background_;
    }
    return nullptr;
  }

  std::optional<TimeTicks> NextTaskTimeLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    std::optional<TimeTicks> next;
    if (!control_.empty()) {
      next = control_.front().due;
    }
    if (!background_.empty() &&
        (!next.has_value() || background_.front().due < *next)) {
      next = background_.front().due;
    }
    return next;
  }

  const TickClock& clock_;
  const WakeUpCallback wake_up_;
  const Ordering ordering_;
  std::recursive_mutex wake_lock_;
  Lock lock_;
  std::vector<ScheduledTask> control_ ABSL_GUARDED_BY(lock_);
  std::vector<ScheduledTask> background_ ABSL_GUARDED_BY(lock_);
  std::uint64_t next_order_ ABSL_GUARDED_BY(lock_) = 0;
  bool shut_down_ ABSL_GUARDED_BY(lock_) = false;
};

class SingleThreadTaskExecutor::Sequence final : public SequencedTaskRunner {
 public:
  Sequence(std::weak_ptr<Core> core, TaskPriority priority)
      : core_(std::move(core)),
        priority_(priority),
        token_(SequenceToken::Create()) {}

  bool RunsTasksInCurrentSequence() const override {
    return token_ == SequenceToken::GetForCurrentThread();
  }

 protected:
  bool DoPostTask(Task task, absl::Duration delay,
                  std::source_location posted_from) override {
    const std::shared_ptr<Core> core = core_.lock();
    if (core == nullptr) {
      return false;
    }
    return core->Post(token_, priority_, std::move(task), delay, posted_from);
  }

 private:
  const std::weak_ptr<Core> core_;
  const TaskPriority priority_;
  const SequenceToken token_;
};

SingleThreadTaskExecutor::SingleThreadTaskExecutor(const TickClock& clock,
                                                   WakeUpCallback wake_up,
                                                   Ordering ordering)
    : core_(std::make_shared<Core>(clock, std::move(wake_up), ordering)) {}

SingleThreadTaskExecutor::~SingleThreadTaskExecutor() { Shutdown(); }

std::shared_ptr<SequencedTaskRunner>
SingleThreadTaskExecutor::CreateSequencedTaskRunner(TaskPriority priority) {
  return std::make_shared<Sequence>(core_, priority);
}

void SingleThreadTaskExecutor::RunUntilIdle() {
  while (core_->RunNextDueTask()) {
  }
}

bool SingleThreadTaskExecutor::RunFor(absl::Duration budget) {
  const TimeTicks start = core_->clock().NowTicks();
  while (core_->clock().NowTicks() - start < budget) {
    if (!core_->RunNextDueTask()) {
      return false;
    }
  }
  return core_->HasDueTask();
}

std::optional<TimeTicks> SingleThreadTaskExecutor::NextTaskTime() const {
  return core_->NextTaskTime();
}

void SingleThreadTaskExecutor::Shutdown() { core_->Shutdown(); }

}
