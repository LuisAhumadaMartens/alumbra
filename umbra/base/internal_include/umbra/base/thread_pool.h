#ifndef UMBRA_BASE_THREAD_POOL_H_
#define UMBRA_BASE_THREAD_POOL_H_

#include <memory>
#include <vector>

#include "penumbra/interface/thread.h"
#include "umbra/base/task_runner.h"

namespace umbra::base {

penumbra::ThreadFactory& GetDefaultThreadFactory();

int DefaultMaxWorkers();

struct ThreadPoolOptions {
  int max_workers = 0;
  penumbra::ThreadFactory* thread_factory = nullptr;
};

class ThreadPool final : public TaskExecutor {
 public:
  explicit ThreadPool(ThreadPoolOptions options = {});
  ~ThreadPool() override;

  std::shared_ptr<SequencedTaskRunner> CreateSequencedTaskRunner(
      TaskPriority priority) override;

  void Shutdown();

  int worker_count() const { return worker_count_; }

 private:
  class Core;
  class Sequence;

  std::shared_ptr<Core> core_;
  std::vector<std::unique_ptr<penumbra::ThreadHandle>> workers_;
  int worker_count_ = 0;
  bool shut_down_ = false;
};

}

#endif
