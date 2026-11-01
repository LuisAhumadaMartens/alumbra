#ifndef UMBRA_BASE_THREAD_POOL_H_
#define UMBRA_BASE_THREAD_POOL_H_

#include <memory>
#include <vector>

#include "penumbra/interface/thread.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"

namespace umbra::base {

penumbra::ThreadFactory& GetDefaultThreadFactory();

int DefaultMaxWorkers();

struct ThreadPoolOptions {
  int max_workers = 0;
  penumbra::ThreadFactory* thread_factory = nullptr;
  const TickClock* tick_clock = nullptr;
};

class ThreadPool final : public TaskExecutor {
 public:
  explicit ThreadPool(ThreadPoolOptions options = {});
  [[nodiscard]] static absl::StatusOr<std::unique_ptr<ThreadPool>> Create(
      ThreadPoolOptions options = {});
  ~ThreadPool() override;
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  std::shared_ptr<SequencedTaskRunner> CreateSequencedTaskRunner(
      TaskPriority priority) override;

  void Shutdown();

  [[nodiscard]] int worker_count() const { return worker_count_; }

 private:
  class Core;
  class Sequence;
  struct DeferStart {};

  ThreadPool(ThreadPoolOptions options, DeferStart);
  [[nodiscard]] absl::Status StartWorkers(ThreadPoolOptions options);

  std::shared_ptr<Core> core_;
  std::vector<std::unique_ptr<penumbra::ThreadHandle>> workers_;
  int worker_count_ = 0;
  bool shut_down_ = false;
};

}

#endif
