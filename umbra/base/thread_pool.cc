#include "umbra/base/thread_pool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "penumbra/interface/thread.h"
#include "umbra/base/lock.h"
#include "umbra/base/sequence_token.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/time_ticks.h"

namespace umbra::base {
namespace {

class StdThreadHandle final : public penumbra::ThreadHandle {
 public:
  explicit StdThreadHandle(std::thread thread) : thread_(std::move(thread)) {}
  ~StdThreadHandle() override { Join(); }

  void Join() override {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::thread thread_;
};

class StdThreadFactory final : public penumbra::ThreadFactory {
 public:
  std::unique_ptr<penumbra::ThreadHandle> StartThread(
      std::string_view, std::function<void()> body) override {
    return std::make_unique<StdThreadHandle>(std::thread(std::move(body)));
  }
};

struct PendingTask {
  TaskRunner::Task task;
  std::source_location posted_from;
};

}

penumbra::ThreadFactory& GetDefaultThreadFactory() {
  static StdThreadFactory* const factory = new StdThreadFactory;
  return *factory;
}

int DefaultMaxWorkers() {
  const int hardware = static_cast<int>(std::thread::hardware_concurrency());
  return std::clamp(hardware - 1, 2, 4);
}

class ThreadPool::Sequence final
    : public SequencedTaskRunner,
      public std::enable_shared_from_this<Sequence> {
 public:
  Sequence(std::weak_ptr<Core> core, TaskPriority priority)
      : core_(std::move(core)),
        priority_(priority),
        token_(SequenceToken::Create()) {}

  bool RunsTasksInCurrentSequence() const override {
    return token_ == SequenceToken::GetForCurrentThread();
  }

  TaskPriority priority() const { return priority_; }

  bool Enqueue(PendingTask pending) {
    AutoLock lock(lock_);
    queue_.push_back(std::move(pending));
    if (scheduled_) {
      return false;
    }
    scheduled_ = true;
    return true;
  }

  void RunNextTask(Core& core);

  void DropTasks() {
    std::deque<PendingTask> dropped;
    {
      AutoLock lock(lock_);
      dropped.swap(queue_);
      scheduled_ = false;
    }
  }

 protected:
  bool DoPostTask(Task task, absl::Duration delay,
                  std::source_location posted_from) override;

 private:
  const std::weak_ptr<Core> core_;
  const TaskPriority priority_;
  const SequenceToken token_;
  Lock lock_;
  std::deque<PendingTask> queue_ ABSL_GUARDED_BY(lock_);
  bool scheduled_ ABSL_GUARDED_BY(lock_) = false;
};

class ThreadPool::Core {
 public:
  explicit Core(const TickClock& clock) : clock_(clock) {}

  bool PostNow(const std::shared_ptr<Sequence>& sequence, PendingTask pending) {
    {
      AutoLock lock(lock_);
      if (shutting_down_) {
        return false;
      }
    }
    if (sequence->Enqueue(std::move(pending)) && !Schedule(sequence)) {
      sequence->DropTasks();
    }
    return true;
  }

  bool PostDelayed(const std::shared_ptr<Sequence>& sequence,
                   PendingTask pending, absl::Duration delay) {
    const TimeTicks due = clock_.NowTicks() + delay;
    AutoLock lock(lock_);
    if (shutting_down_) {
      return false;
    }
    delayed_.push_back(DelayedTask{.due = due,
                                   .order = next_order_++,
                                   .sequence = sequence,
                                   .pending = std::move(pending)});
    std::push_heap(delayed_.begin(), delayed_.end(), RunsLater);
    work_available_.Signal();
    return true;
  }

  bool Schedule(const std::shared_ptr<Sequence>& sequence) {
    AutoLock lock(lock_);
    if (shutting_down_) {
      return false;
    }
    ReadyQueueFor(sequence->priority()).push_back(sequence);
    work_available_.Signal();
    return true;
  }

  void RunWorker() {
    for (;;) {
      std::shared_ptr<Sequence> next;
      {
        AutoLock lock(lock_);
        for (;;) {
          if (shutting_down_) {
            return;
          }
          PromoteDueTasks();
          if (!control_ready_.empty()) {
            next = std::move(control_ready_.front());
            control_ready_.pop_front();
            break;
          }
          if (!background_ready_.empty()) {
            next = std::move(background_ready_.front());
            background_ready_.pop_front();
            break;
          }
          if (delayed_.empty()) {
            work_available_.Wait(lock_);
          } else {
            work_available_.WaitFor(lock_,
                                    delayed_.front().due - clock_.NowTicks());
          }
        }
      }
      next->RunNextTask(*this);
    }
  }

  void Shutdown() {
    std::deque<std::shared_ptr<Sequence>> control;
    std::deque<std::shared_ptr<Sequence>> background;
    std::vector<DelayedTask> delayed;
    {
      AutoLock lock(lock_);
      shutting_down_ = true;
      control.swap(control_ready_);
      background.swap(background_ready_);
      delayed.swap(delayed_);
      work_available_.Broadcast();
    }
    for (const std::shared_ptr<Sequence>& sequence : control) {
      sequence->DropTasks();
    }
    for (const std::shared_ptr<Sequence>& sequence : background) {
      sequence->DropTasks();
    }
  }

 private:
  struct DelayedTask {
    TimeTicks due;
    std::uint64_t order = 0;
    std::shared_ptr<Sequence> sequence;
    PendingTask pending;
  };

  static bool RunsLater(const DelayedTask& lhs, const DelayedTask& rhs) {
    if (lhs.due != rhs.due) {
      return lhs.due > rhs.due;
    }
    return lhs.order > rhs.order;
  }

  std::deque<std::shared_ptr<Sequence>>& ReadyQueueFor(TaskPriority priority)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return priority == TaskPriority::kControl ? control_ready_
                                              : background_ready_;
  }

  void PromoteDueTasks() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    const TimeTicks now = clock_.NowTicks();
    while (!delayed_.empty() && delayed_.front().due <= now) {
      std::pop_heap(delayed_.begin(), delayed_.end(), RunsLater);
      DelayedTask due = std::move(delayed_.back());
      delayed_.pop_back();
      if (due.sequence->Enqueue(std::move(due.pending))) {
        ReadyQueueFor(due.sequence->priority()).push_back(due.sequence);
        work_available_.Signal();
      }
    }
  }

  const TickClock& clock_;
  Lock lock_;
  ConditionVariable work_available_;
  std::deque<std::shared_ptr<Sequence>> control_ready_ ABSL_GUARDED_BY(lock_);
  std::deque<std::shared_ptr<Sequence>> background_ready_
      ABSL_GUARDED_BY(lock_);
  std::vector<DelayedTask> delayed_ ABSL_GUARDED_BY(lock_);
  std::uint64_t next_order_ ABSL_GUARDED_BY(lock_) = 0;
  bool shutting_down_ ABSL_GUARDED_BY(lock_) = false;
};

void ThreadPool::Sequence::RunNextTask(Core& core) {
  PendingTask pending;
  {
    AutoLock lock(lock_);
    if (queue_.empty()) {
      scheduled_ = false;
      return;
    }
    pending = std::move(queue_.front());
    queue_.pop_front();
  }
  {
    ScopedTaskContext context(token_, pending.posted_from);
    std::move(pending.task)();
    pending.task = nullptr;
  }
  bool reschedule = false;
  {
    AutoLock lock(lock_);
    if (queue_.empty()) {
      scheduled_ = false;
    } else {
      reschedule = true;
    }
  }
  if (reschedule && !core.Schedule(shared_from_this())) {
    DropTasks();
  }
}

bool ThreadPool::Sequence::DoPostTask(Task task, absl::Duration delay,
                                      std::source_location posted_from) {
  const std::shared_ptr<Core> core = core_.lock();
  if (core == nullptr) {
    return false;
  }
  PendingTask pending{.task = std::move(task), .posted_from = posted_from};
  if (delay > absl::ZeroDuration()) {
    return core->PostDelayed(shared_from_this(), std::move(pending), delay);
  }
  return core->PostNow(shared_from_this(), std::move(pending));
}

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : core_(std::make_shared<Core>(GetDefaultTickClock())),
      worker_count_(options.max_workers > 0 ? options.max_workers
                                            : DefaultMaxWorkers()) {
  penumbra::ThreadFactory& factory = options.thread_factory != nullptr
                                         ? *options.thread_factory
                                         : GetDefaultThreadFactory();
  workers_.reserve(static_cast<std::size_t>(worker_count_));
  for (int i = 0; i < worker_count_; ++i) {
    workers_.push_back(
        factory.StartThread("umbra-worker-" + std::to_string(i),
                            [core = core_]() { core->RunWorker(); }));
  }
}

ThreadPool::~ThreadPool() { Shutdown(); }

std::shared_ptr<SequencedTaskRunner> ThreadPool::CreateSequencedTaskRunner(
    TaskPriority priority) {
  return std::make_shared<Sequence>(core_, priority);
}

void ThreadPool::Shutdown() {
  if (shut_down_) {
    return;
  }
  shut_down_ = true;
  core_->Shutdown();
  for (const std::unique_ptr<penumbra::ThreadHandle>& worker : workers_) {
    worker->Join();
  }
  workers_.clear();
}

}
