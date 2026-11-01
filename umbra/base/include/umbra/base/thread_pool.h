#ifndef UMBRA_BASE_THREAD_POOL_H_
#define UMBRA_BASE_THREAD_POOL_H_

#include <memory>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "umbra/base/task_runner.h"

namespace umbra::base {

class ThreadHandle {
 public:
  virtual ~ThreadHandle() = default;

  virtual void Join() = 0;
};

class ThreadFactory {
 public:
  virtual ~ThreadFactory() = default;

  virtual std::unique_ptr<ThreadHandle> StartThread(
      std::string_view name, absl::AnyInvocable<void() &&> body) = 0;
};

ThreadFactory& GetDefaultThreadFactory();

int DefaultMaxWorkers();

struct ThreadPoolOptions {
  int max_workers = 0;
  ThreadFactory* thread_factory = nullptr;
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
  std::vector<std::unique_ptr<ThreadHandle>> workers_;
  int worker_count_ = 0;
  bool shut_down_ = false;
};

}

#endif
