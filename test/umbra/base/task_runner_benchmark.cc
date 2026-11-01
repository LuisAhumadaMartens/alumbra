#include <atomic>
#include <cstdint>
#include <memory>

#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/waitable_event.h"
#include "umbra/base/thread_pool.h"

namespace umbra::base {
namespace {

constexpr int kTasksPerBatch = 1000;

void BM_ThreadPoolPostAndRunBatch(benchmark::State& state) {
  ThreadPool pool({.max_workers = 2});
  auto runner = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  for (auto _ : state) {
    std::atomic<int> remaining{kTasksPerBatch};
    WaitableEvent done;
    for (int i = 0; i < kTasksPerBatch; ++i) {
      runner->PostTask([&remaining, &done] {
        if (remaining.fetch_sub(1) == 1) {
          done.Signal();
        }
      });
    }
    done.Wait();
  }
  state.SetItemsProcessed(state.iterations() * kTasksPerBatch);
}
BENCHMARK(BM_ThreadPoolPostAndRunBatch)->UseRealTime();

void BM_ThreadPoolCrossSequenceRoundTrip(benchmark::State& state) {
  ThreadPool pool({.max_workers = 2});
  auto first = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  auto second = pool.CreateSequencedTaskRunner(TaskPriority::kControl);
  for (auto _ : state) {
    WaitableEvent done;
    first->PostTask(
        [&second, &done] { second->PostTask([&done] { done.Signal(); }); });
    done.Wait();
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ThreadPoolCrossSequenceRoundTrip)->UseRealTime();

void BM_SingleThreadPostAndRunBatch(benchmark::State& state) {
  SingleThreadTaskExecutor executor;
  auto runner = executor.CreateSequencedTaskRunner(TaskPriority::kControl);
  std::int64_t ran = 0;
  for (auto _ : state) {
    for (int i = 0; i < kTasksPerBatch; ++i) {
      runner->PostTask([&ran] { ++ran; });
    }
    executor.RunUntilIdle();
  }
  benchmark::DoNotOptimize(ran);
  state.SetItemsProcessed(state.iterations() * kTasksPerBatch);
}
BENCHMARK(BM_SingleThreadPostAndRunBatch);

}
}
