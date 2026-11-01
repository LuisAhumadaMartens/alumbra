#include "umbra/base/thread_pool.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/lock.h"
#include "umbra/base/task_runner.h"
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
  ASSERT_TRUE(delayed.WaitForAll());
  EXPECT_FALSE(busy->PostTask([] {}));
  EXPECT_FALSE(running_task_finished);
  release.Signal();
  shutdown.join();

  EXPECT_TRUE(running_task_finished);
  EXPECT_EQ(pending_tasks_run.load(), 0);
  EXPECT_EQ(queued.count(), 3);
  EXPECT_EQ(delayed.count(), 1);
}

class RecordingThreadFactory final : public ThreadFactory {
 public:
  std::unique_ptr<ThreadHandle> StartThread(
      std::string_view name, absl::AnyInvocable<void() &&> body) override {
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

}
}
