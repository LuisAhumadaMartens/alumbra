#ifndef UMBRA_SOURCE_SEQUENCED_RECEIVER_H_
#define UMBRA_SOURCE_SEQUENCED_RECEIVER_H_

#include <atomic>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "umbra/base/task_runner.h"

namespace umbra::source {

template <typename Target>
class SequencedReceiver
    : public std::enable_shared_from_this<SequencedReceiver<Target>> {
 public:
  static std::shared_ptr<SequencedReceiver> Create(
      std::shared_ptr<base::SequencedTaskRunner> runner, Target* target) {
    return std::make_shared<SequencedReceiver>(std::move(runner), target);
  }

  SequencedReceiver(std::shared_ptr<base::SequencedTaskRunner> runner,
                    Target* target)
      : runner_(std::move(runner)), target_(target) {}
  SequencedReceiver(const SequencedReceiver&) = delete;
  SequencedReceiver& operator=(const SequencedReceiver&) = delete;
  ~SequencedReceiver() = default;

  template <typename Handler>
  absl::Status Post(Handler handler) {
    if (cancelled_.load(std::memory_order_acquire)) {
      return absl::OkStatus();
    }
    const bool posted = runner_->PostTask(
        [self = this->shared_from_this(),
         handler = std::move(handler)]() mutable { self->Run(handler); });
    return posted ? absl::OkStatus()
                  : absl::UnavailableError("the network sequence has stopped");
  }

  void Cancel() {
    cancelled_.store(true, std::memory_order_release);
    target_ = nullptr;
  }

  [[nodiscard]] bool Cancelled() const {
    return cancelled_.load(std::memory_order_acquire);
  }

 private:
  template <typename Handler>
  void Run(Handler& handler) {
    if (target_ == nullptr || cancelled_.load(std::memory_order_acquire)) {
      return;
    }
    handler(*target_);
  }

  std::shared_ptr<base::SequencedTaskRunner> runner_;
  Target* target_;
  std::atomic<bool> cancelled_{false};
};

}

#endif
