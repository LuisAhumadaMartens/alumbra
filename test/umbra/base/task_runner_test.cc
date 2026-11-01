#include "umbra/base/task_runner.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/time/time.h"
#include "executor_harness.h"
#include "gtest/gtest.h"
#include "umbra/base/lock.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/sequence_token.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/weak_ptr.h"

namespace umbra::base {
namespace {

class Recorder {
 public:
  void Add(std::string entry) {
    AutoLock const lock(lock_);
    entries_.push_back(std::move(entry));
  }

  std::vector<std::string> entries() {
    AutoLock const lock(lock_);
    return entries_;
  }

 private:
  Lock lock_;
  std::vector<std::string> entries_ ABSL_GUARDED_BY(lock_);
};

class TaskRunnerTest : public testing::TestWithParam<ExecutorKind> {
 protected:
  TaskRunnerTest() : harness_(GetParam()) {}

  std::shared_ptr<SequencedTaskRunner> CreateRunner(
      TaskPriority priority = TaskPriority::kBackground) {
    return harness_.executor().CreateSequencedTaskRunner(priority);
  }

  ExecutorHarness harness_;
};

TEST_P(TaskRunnerTest, RunsTasksInPostingOrder) {
  auto runner = CreateRunner();
  Recorder recorder;
  WaitableEvent done;
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(
        runner->PostTask([&recorder, i] { recorder.Add(std::to_string(i)); }));
  }
  runner->PostTask([&done] { done.Signal(); });
  ASSERT_TRUE(harness_.RunUntil(done));

  std::vector<std::string> expected;
  expected.reserve(100);
  for (int i = 0; i < 100; ++i) {
    expected.push_back(std::to_string(i));
  }
  EXPECT_EQ(recorder.entries(), expected);
}

TEST_P(TaskRunnerTest, NeverRunsTwoTasksOfOneSequenceAtOnce) {
  auto runner = CreateRunner();
  constexpr int kThreads = 8;
  constexpr int kTasksPerThread = 500;
  std::atomic<bool> running{false};
  std::atomic<bool> overlapped{false};
  std::atomic<int> completed{0};
  std::vector<int> last_index(kThreads, -1);
  std::atomic<bool> out_of_order{false};
  WaitableEvent done;

  std::vector<std::thread> posters;
  posters.reserve(kThreads);
  for (int thread = 0; thread < kThreads; ++thread) {
    posters.emplace_back([&, thread] {
      for (int index = 0; index < kTasksPerThread; ++index) {
        runner->PostTask([&, thread, index] {
          if (running.exchange(true)) {
            overlapped = true;
          }
          if (last_index[thread] != index - 1) {
            out_of_order = true;
          }
          last_index[thread] = index;
          running = false;
          if (completed.fetch_add(1) + 1 == kThreads * kTasksPerThread) {
            done.Signal();
          }
        });
      }
    });
  }
  const bool finished = harness_.RunUntil(done);
  for (std::thread& poster : posters) {
    poster.join();
  }
  ASSERT_TRUE(finished);
  EXPECT_FALSE(overlapped);
  EXPECT_FALSE(out_of_order);
  EXPECT_EQ(completed.load(), kThreads * kTasksPerThread);
}

TEST_P(TaskRunnerTest, ReportsTheCurrentSequence) {
  auto first = CreateRunner();
  auto second = CreateRunner();
  EXPECT_FALSE(first->RunsTasksInCurrentSequence());

  std::atomic<bool> on_first{false};
  std::atomic<bool> on_second{true};
  WaitableEvent done;
  first->PostTask([&] {
    on_first = first->RunsTasksInCurrentSequence();
    on_second = second->RunsTasksInCurrentSequence();
    done.Signal();
  });
  ASSERT_TRUE(harness_.RunUntil(done));
  EXPECT_TRUE(on_first);
  EXPECT_FALSE(on_second);
}

TaskRunner::Task CapturePostedFrom(std::source_location* seen,
                                   WaitableEvent* done) {
  return [seen, done] {
    *seen = GetCurrentTaskPostedFrom();
    done->Signal();
  };
}

TEST_P(TaskRunnerTest, RecordsWhereATaskWasPostedFrom) {
  auto runner = CreateRunner();
  std::source_location seen;
  WaitableEvent done;
  const int line = __LINE__ + 1;
  runner->PostTask(CapturePostedFrom(&seen, &done));
  ASSERT_TRUE(harness_.RunUntil(done));
  EXPECT_EQ(seen.line(), static_cast<unsigned>(line));
  EXPECT_TRUE(
      std::string_view(seen.file_name()).ends_with("task_runner_test.cc"));
}

struct Owner {
  int hits = 0;
  SequenceChecker sequence_checker;
  WeakPtrFactory<Owner> weak_factory{this};
};

TEST_P(TaskRunnerTest, DropsTasksBoundToADestroyedOwner) {
  auto runner = CreateRunner();
  std::unique_ptr<Owner> owner;
  std::atomic<bool> hit{false};
  std::atomic<bool> on_owner_sequence{false};
  WaitableEvent done;
  runner->PostTask([&] {
    owner = std::make_unique<Owner>();
    WeakPtr<Owner> const weak = owner->weak_factory.GetWeakPtr();
    runner->PostTask([&] {
      on_owner_sequence = owner->sequence_checker.CalledOnValidSequence();
      owner.reset();
    });
    runner->PostDelayedTask(
        [weak, &hit, &done] {
          if (weak) {
            hit = true;
          }
          done.Signal();
        },
        absl::Milliseconds(5));
  });
  ASSERT_TRUE(harness_.RunUntil(done));
  EXPECT_TRUE(on_owner_sequence);
  EXPECT_FALSE(hit);
}

TEST_P(TaskRunnerTest, RejectsTasksAfterShutdown) {
  auto runner = CreateRunner();
  harness_.Shutdown();
  EXPECT_FALSE(runner->PostTask([] {}));
  EXPECT_FALSE(runner->PostDelayedTask([] {}, absl::Milliseconds(1)));
}

std::vector<std::string> RunHopChain(ExecutorHarness& harness) {
  constexpr int kHops = 60;
  std::vector<std::shared_ptr<SequencedTaskRunner>> runners = {
      harness.executor().CreateSequencedTaskRunner(TaskPriority::kControl),
      harness.executor().CreateSequencedTaskRunner(TaskPriority::kBackground),
      harness.executor().CreateSequencedTaskRunner(TaskPriority::kBackground),
  };
  Recorder recorder;
  WaitableEvent done;
  struct Hop {
    std::vector<std::shared_ptr<SequencedTaskRunner>>* runners;
    Recorder* recorder;
    WaitableEvent* done;

    void Run(int index) const {
      const std::size_t sequence = static_cast<std::size_t>(index) % 3;
      recorder->Add(
          std::to_string(index) + "@" + std::to_string(sequence) + ":" +
          ((*runners)[sequence]->RunsTasksInCurrentSequence() ? "on" : "off"));
      if (index + 1 == kHops) {
        done->Signal();
        return;
      }
      const std::size_t next = static_cast<std::size_t>(index + 1) % 3;
      Hop const hop = *this;
      if (index % 4 == 3) {
        (*runners)[next]->PostDelayedTask([hop, index] { hop.Run(index + 1); },
                                          absl::Microseconds(50));
      } else {
        (*runners)[next]->PostTask([hop, index] { hop.Run(index + 1); });
      }
    }
  };
  const Hop first{.runners=&runners, .recorder=&recorder, .done=&done};
  runners[0]->PostTask([first] { first.Run(0); });
  EXPECT_TRUE(harness.RunUntil(done));
  return recorder.entries();
}

TEST_P(TaskRunnerTest, ChainedTimersAcrossRunnersProduceAnIdenticalTrace) {
  const std::vector<std::string> reference = RunHopChain(harness_);
  ASSERT_EQ(reference.size(), 60U);
  for (int iteration = 0; iteration < 1000; ++iteration) {
    ASSERT_EQ(RunHopChain(harness_), reference) << "iteration " << iteration;
  }
}

INSTANTIATE_TEST_SUITE_P(Executors, TaskRunnerTest,
                         testing::Values(ExecutorKind::kThreadPool,
                                         ExecutorKind::kSingleThread),
                         [](const testing::TestParamInfo<ExecutorKind>& info) {
                           return ExecutorKindName(info.param);
                         });

}
}
