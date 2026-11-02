#ifndef UMBRA_UI_MACHINE_H_
#define UMBRA_UI_MACHINE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "umbra/base/lock.h"
#include "umbra/base/tick_clock.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {

class UiMachine {
 public:
  using FrameRequestHook = std::function<void()>;

  UiMachine(const base::TickClock& clock, UiConfig config);
  ~UiMachine() = default;
  UiMachine(const UiMachine&) = delete;
  UiMachine& operator=(const UiMachine&) = delete;

  [[nodiscard]] std::vector<SequencedEffect> Dispatch(const Input& input);
  [[nodiscard]] std::shared_ptr<const ViewModel> TakeSnapshot();
  void SetFrameRequestHook(FrameRequestHook hook);
  [[nodiscard]] Micros NowMicros() const;
  [[nodiscard]] const UiState& State() const { return state_; }
  [[nodiscard]] const UiConfig& Config() const { return config_; }

 private:
  const base::TickClock& clock_;
  UiConfig config_;
  UiState state_;
  std::uint64_t next_effect_ = 1;
  FrameRequestHook frame_hook_;
  std::atomic<bool> frame_requested_{false};
  std::atomic<std::uint64_t> delivered_announcement_{0};
  base::Lock lock_;
  std::shared_ptr<const ViewModel> latest_ ABSL_GUARDED_BY(lock_);
};

}

#endif
