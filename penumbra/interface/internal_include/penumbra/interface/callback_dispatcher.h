#ifndef PENUMBRA_INTERFACE_CALLBACK_DISPATCHER_H_
#define PENUMBRA_INTERFACE_CALLBACK_DISPATCHER_H_

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <utility>

#include "penumbra/interface/player.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra {

class CallbackScope {
 public:
  CallbackScope() { ++depth_; }
  ~CallbackScope() { --depth_; }
  CallbackScope(const CallbackScope&) = delete;
  CallbackScope& operator=(const CallbackScope&) = delete;
  static bool IsActive() { return depth_ != 0; }

 private:
  inline static thread_local unsigned depth_ = 0;
};

class PortCallScope {
 public:
  PortCallScope() { assert(!CallbackScope::IsActive()); }
};

class CallbackDispatcher final : public PlayerCallbacks {
 public:
  CallbackDispatcher(SeekTicket ticket,
                     std::shared_ptr<ControlTaskRunner> control,
                     std::shared_ptr<PlayerCallbacks> target)
      : state_(std::make_shared<State>(ticket, std::move(control),
                                       std::move(target))) {
    assert(ticket != 0 && state_->control != nullptr &&
           state_->target != nullptr);
  }

  absl::Status OnEvent(PlayerEvent event) override {
    auto state = state_;
    if (state->stopped.load() || event.ticket != state->ticket.load()) {
      return absl::OkStatus();
    }
    return state->control->PostTask(
        [state, event = std::move(event)]() mutable {
          assert(state->control->RunsTasksInCurrentSequence());
          if (state->stopped.load() || event.ticket != state->ticket.load()) {
            return;
          }
          CallbackScope scope;
          auto status = state->target->OnEvent(std::move(event));
          if (!status.ok()) {
            std::lock_guard lock(state->delivery_mutex);
            state->delivery_status = std::move(status);
          }
        },
        absl::ZeroDuration());
  }

  absl::Status AdvanceTicket(SeekTicket ticket) {
    PortCallScope scope;
    assert(state_->control->RunsTasksInCurrentSequence());
    if (state_->stopped.load()) {
      return absl::FailedPreconditionError("callback dispatcher is stopped");
    }
    if (ticket <= state_->ticket.load()) {
      return absl::InvalidArgumentError("seek ticket must increase");
    }
    state_->ticket.store(ticket);
    return absl::OkStatus();
  }

  void Stop() {
    PortCallScope scope;
    assert(state_->control->RunsTasksInCurrentSequence());
    state_->stopped.store(true);
  }

  absl::Status GetDeliveryStatus() const {
    std::lock_guard lock(state_->delivery_mutex);
    return state_->delivery_status;
  }

 private:
  struct State {
    State(SeekTicket value, std::shared_ptr<ControlTaskRunner> runner,
          std::shared_ptr<PlayerCallbacks> callbacks)
        : ticket(value),
          control(std::move(runner)),
          target(std::move(callbacks)) {}
    std::atomic<SeekTicket> ticket;
    std::atomic<bool> stopped{false};
    std::mutex delivery_mutex;
    absl::Status delivery_status;
    std::shared_ptr<ControlTaskRunner> control;
    std::shared_ptr<PlayerCallbacks> target;
  };
  std::shared_ptr<State> state_;
};

}

#endif
