#include "umbra/base/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/lock.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/fake_tick_clock.h"
#include "umbra/base/test_support/waitable_event.h"

namespace umbra::base {
namespace {

TEST(ThreadPoolTest, DerivesWorkersFromHardwareConcurrency) {
  EXPECT_GE(DefaultMaxWorkers(), 2);
  EXPECT_LE(DefaultMaxWorkers(), 4);
  ThreadPool pool;
  EXPECT_EQ(pool.worker_count(), DefaultMaxWorkers());
}

TEST(ThreadPoolTest, RunsControlSequencesBeforeBackgroundSequences) {
  ThreadPool pool({.max_workers = 1});
  auto blocker = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto background = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto control = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  WaitableEvent started;
  WaitableEvent release;
  WaitableEvent done;
  Lock lock;
  std::vector<std::string> order;

  blocker->PostTask([&] {
    started.Signal();
    release.Wait();
  });
  ASSERT_TRUE(started.TimedWait(absl::Seconds(30)));
  background->PostTask([&] {
    AutoLock scoped(lock);
    order.push_back("background");
  });
  control->PostTask([&] {
    AutoLock scoped(lock);
    order.push_back("control");
  });
  background->PostTask([&] { done.Signal(); });
  release.Signal();
  ASSERT_TRUE(done.TimedWait(absl::Seconds(30)));

  AutoLock scoped(lock);
  EXPECT_EQ(order, (std::vector<std::string>{"control", "background"}));
}

class DropCounter {
 public:
  explicit DropCounter(int expected) : expected_(expected) {}

  void Dropped() {
    if (count_.fetch_add(1) + 1 == expected_) {
      all_dropped_.Signal();
    }
  }

  int count() const { return count_.load(); }
  bool WaitForAll() { return all_dropped_.TimedWait(absl::Seconds(30)); }

 private:
  const int expected_;
  std::atomic<int> count_{0};
  WaitableEvent all_dropped_;
};

class CountsDrop {
 public:
  explicit CountsDrop(DropCounter* counter) : counter_(counter) {}
  CountsDrop(CountsDrop&& other) noexcept
      : counter_(std::exchange(other.counter_, nullptr)) {}
  CountsDrop& operator=(CountsDrop&&) = delete;
  ~CountsDrop() {
    if (counter_ != nullptr) {
      counter_->Dropped();
    }
  }

 private:
  DropCounter* counter_;
};

TEST(ThreadPoolTest, ShutdownDropsPendingTasksAndWaitsForTheRunningOne) {
  ThreadPool pool({.max_workers = 2});
  auto busy = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto idle = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  WaitableEvent started;
  WaitableEvent release;
  std::atomic<bool> running_task_finished{false};
  std::atomic<int> pending_tasks_run{0};
  DropCounter queued(3);
  DropCounter delayed(1);

  busy->PostTask([&] {
    started.Signal();
    release.Wait();
    running_task_finished = true;
  });
  ASSERT_TRUE(started.TimedWait(absl::Seconds(30)));
  for (int i = 0; i < 3; ++i) {
    busy->PostTask([&pending_tasks_run, drop = CountsDrop(&queued)] {
      ++pending_tasks_run;
    });
  }
  auto delayed_task = [&pending_tasks_run, drop = CountsDrop(&delayed)] {
    ++pending_tasks_run;
  };
  idle->PostDelayedTask(std::move(delayed_task), absl::Hours(1));

  std::thread shutdown([&pool] { pool.Shutdown(); });
  EXPECT_TRUE(delayed.WaitForAll());
  EXPECT_FALSE(busy->PostTask([] {}));
  EXPECT_FALSE(running_task_finished);
  release.Signal();
  shutdown.join();

  EXPECT_TRUE(running_task_finished);
  EXPECT_EQ(pending_tasks_run.load(), 0);
  EXPECT_EQ(queued.count(), 3);
  EXPECT_EQ(delayed.count(), 1);
}

class RecordingThreadFactory final : public penumbra::ThreadFactory {
 public:
  std::unique_ptr<penumbra::ThreadHandle> StartThread(
      std::string_view name, std::function<void()> body) override {
    {
      AutoLock lock(lock_);
      names_.emplace_back(name);
    }
    return GetDefaultThreadFactory().StartThread(name, std::move(body));
  }

  std::vector<std::string> names() {
    AutoLock lock(lock_);
    return names_;
  }

 private:
  Lock lock_;
  std::vector<std::string> names_ ABSL_GUARDED_BY(lock_);
};

TEST(ThreadPoolTest, StartsWorkersThroughTheThreadFactory) {
  RecordingThreadFactory factory;
  {
    ThreadPool pool({.max_workers = 3, .thread_factory = &factory});
    EXPECT_EQ(pool.worker_count(), 3);
    WaitableEvent done;
    pool.CreateSequencedTaskRunner(TaskPriority::kBackground)
        ->PostTask([&done] { done.Signal(); });
    ASSERT_TRUE(done.TimedWait(absl::Seconds(30)));
  }
  EXPECT_EQ(factory.names(),
            (std::vector<std::string>{"umbra-worker-0", "umbra-worker-1",
                                      "umbra-worker-2"}));
}

TEST(ThreadPoolTest, RejectsTasksOnceThePoolIsDestroyed) {
  std::shared_ptr<SequencedTaskRunner> runner;
  {
    ThreadPool pool({.max_workers = 1});
    runner = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  }
  EXPECT_FALSE(runner->PostTask([] {}));
}

TEST(ThreadPoolTest, ShutdownRejectsPostsAfterTheBorrowedClockIsDestroyed) {
  auto clock = std::make_unique<FakeTickClock>();
  ThreadPool pool({.max_workers = 1, .tick_clock = clock.get()});
  auto runner = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  pool.Shutdown();
  clock.reset();
  EXPECT_FALSE(runner->PostTask([] {}));
  EXPECT_FALSE(runner->PostDelayedTask([] {}, absl::Milliseconds(1)));
}

class FailingThreadFactory final : public penumbra::ThreadFactory {
 public:
  std::unique_ptr<penumbra::ThreadHandle> StartThread(
      std::string_view name, std::function<void()> body) override {
    if (++calls_ == 2) {
      return nullptr;
    }
    return GetDefaultThreadFactory().StartThread(
        name, [this, body = std::move(body)] {
          body();
          exited = true;
        });
  }

  std::atomic<bool> exited{false};

 private:
  int calls_ = 0;
};

TEST(ThreadPoolTest, RejectsNullHandlesAndJoinsStartedWorkers) {
  FailingThreadFactory factory;
  auto pool =
      ThreadPool::Create({.max_workers = 2, .thread_factory = &factory});
  ASSERT_FALSE(pool.ok());
  EXPECT_EQ(pool.status().code(), absl::StatusCode::kInternal);
  auto detail = GetErrorDetail(pool.status());
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ((*detail)->code, ErrorCode::kPlayerInternal);
  EXPECT_TRUE(factory.exited);
}

TEST(ThreadPoolDeathTest, DirectConstructionRejectsNullHandles) {
  EXPECT_DEATH(
      {
        FailingThreadFactory factory;
        ThreadPool pool({.max_workers = 2, .thread_factory = &factory});
      },
      "ThreadFactory returned a null worker handle");
}

TEST(ThreadPoolTest, FallibleCreationStartsWorkers) {
  auto pool = ThreadPool::Create({.max_workers = 1});
  ASSERT_TRUE(pool.ok());
  EXPECT_EQ((*pool)->worker_count(), 1);
}

TEST(ThreadPoolTest, PromotesDueTasksBeforeLaterImmediatePosts) {
  FakeTickClock clock;
  WaitableEvent started;
  WaitableEvent release;
  WaitableEvent done;
  std::vector<std::string> trace;
  ThreadPool pool({.max_workers = 1, .tick_clock = &clock});
  auto blocker = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  auto runner = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  blocker->PostTask([&] {
    started.Signal();
    release.Wait();
  });
  EXPECT_TRUE(started.TimedWait(absl::Seconds(30)));
  runner->PostDelayedTask([&] { trace.push_back("30a"); },
                          absl::Milliseconds(30));
  runner->PostDelayedTask([&] { trace.push_back("10a"); },
                          absl::Milliseconds(10));
  runner->PostDelayedTask([&] { trace.push_back("20a"); },
                          absl::Milliseconds(20));
  runner->PostDelayedTask([&] { trace.push_back("10b"); },
                          absl::Milliseconds(10));
  runner->PostTask([&] { trace.push_back("now"); });
  clock.Advance(absl::Milliseconds(40));
  runner->PostTask([&] { trace.push_back("later"); });
  runner->PostTask([&] { done.Signal(); });
  release.Signal();
  EXPECT_TRUE(done.TimedWait(absl::Seconds(30)));
  pool.Shutdown();
  EXPECT_EQ(trace, (std::vector<std::string>{"now", "10a", "10b", "20a", "30a",
                                             "later"}));
}

class GatedThreadFactory final : public penumbra::ThreadFactory {
 public:
  std::unique_ptr<penumbra::ThreadHandle> StartThread(
      std::string_view name, std::function<void()> body) override {
    return GetDefaultThreadFactory().StartThread(
        name, [this, body = std::move(body)] {
          ready.Signal();
          release.Wait();
          body();
        });
  }

  WaitableEvent ready;
  WaitableEvent release;
};

TEST(ThreadPoolTest, ShutdownDropsReadyTasksBeforeWorkersCanClaimThem) {
  GatedThreadFactory factory;
  DropCounter counter(1);
  std::atomic<bool> ran{false};
  ThreadPool pool({.max_workers = 1, .thread_factory = &factory});
  auto runner = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  EXPECT_TRUE(factory.ready.TimedWait(absl::Seconds(30)));
  runner->PostTask([&ran, drop = CountsDrop(&counter)] { ran = true; });
  std::thread shutdown([&] { pool.Shutdown(); });
  EXPECT_TRUE(counter.WaitForAll());
  factory.release.Signal();
  shutdown.join();
  EXPECT_FALSE(ran);
}

class TimerHandoffClock final : public TickClock {
 public:
  TimerHandoffClock() : owner_(std::this_thread::get_id()) {}

  TimeTicks NowTicks() const override {
    AutoLock lock(lock_);
    const auto thread = std::this_thread::get_id();
    if (thread != owner_) {
      if (watch_ready_) {
        handoff_readers_.push_back(thread);
        if (busy_worker_ != std::thread::id{} && thread != busy_worker_) {
          timer_handoff.Signal();
        }
      } else if (!armed_) {
        if (std::find(workers_.begin(), workers_.end(), thread) ==
            workers_.end()) {
          workers_.push_back(thread);
          if (workers_.size() == 2) {
            workers_ready.Signal();
          }
        }
      } else if (timer_worker_ == std::thread::id{}) {
        timer_worker_ = thread;
        timer_reads_ = 1;
      } else if (thread == timer_worker_) {
        if (++timer_reads_ == 2) {
          timer_waiting.Signal();
        }
      }
    }
    return {};
  }

  void Arm() {
    AutoLock lock(lock_);
    armed_ = true;
  }

  void WatchReadyTask() {
    AutoLock lock(lock_);
    watch_ready_ = true;
  }

  void MarkBusyWorker() {
    AutoLock lock(lock_);
    busy_worker_ = std::this_thread::get_id();
    for (const auto thread : handoff_readers_) {
      if (thread != busy_worker_) {
        timer_handoff.Signal();
      }
    }
  }

  mutable WaitableEvent workers_ready;
  mutable WaitableEvent timer_waiting;
  mutable WaitableEvent timer_handoff;

 private:
  const std::thread::id owner_;
  mutable Lock lock_;
  mutable std::vector<std::thread::id> workers_ ABSL_GUARDED_BY(lock_);
  bool armed_ ABSL_GUARDED_BY(lock_) = false;
  bool watch_ready_ ABSL_GUARDED_BY(lock_) = false;
  std::thread::id busy_worker_ ABSL_GUARDED_BY(lock_);
  mutable std::vector<std::thread::id> handoff_readers_ ABSL_GUARDED_BY(lock_);
  mutable std::thread::id timer_worker_ ABSL_GUARDED_BY(lock_);
  mutable int timer_reads_ ABSL_GUARDED_BY(lock_) = 0;
};

TEST(ThreadPoolTest, IdleWorkerTakesOverTimerWhileTheTimerWorkerRunsATask) {
  TimerHandoffClock clock;
  WaitableEvent started;
  WaitableEvent release;
  ThreadPool pool({.max_workers = 2, .tick_clock = &clock});
  auto timer = pool.CreateSequencedTaskRunner(TaskPriority::kBackground);
  auto busy = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  EXPECT_TRUE(clock.workers_ready.TimedWait(absl::Seconds(30)));
  clock.Arm();
  timer->PostDelayedTask([] {}, absl::Hours(1));
  EXPECT_TRUE(clock.timer_waiting.TimedWait(absl::Seconds(30)));
  clock.WatchReadyTask();
  busy->PostTask([&] {
    clock.MarkBusyWorker();
    started.Signal();
    release.Wait();
  });
  EXPECT_TRUE(started.TimedWait(absl::Seconds(30)));
  EXPECT_TRUE(clock.timer_handoff.TimedWait(absl::Seconds(30)));
  release.Signal();
  pool.Shutdown();
}

}
}
