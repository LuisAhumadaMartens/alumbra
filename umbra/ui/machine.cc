#include "umbra/ui/machine.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "umbra/base/lock.h"
#include "umbra/base/tick_clock.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/reduce.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {

UiMachine::UiMachine(const base::TickClock& clock, UiConfig config)
    : clock_(clock), config_(std::move(config)), state_(InitialState()) {
  auto first =
      std::make_shared<const ViewModel>(BuildViewModel(state_, config_, {}));
  const base::AutoLock lock(lock_);
  latest_ = std::move(first);
}

Micros UiMachine::NowMicros() const {
  return absl::ToInt64Microseconds(clock_.NowTicks().SinceOrigin());
}

void UiMachine::SetFrameRequestHook(FrameRequestHook hook) {
  frame_hook_ = std::move(hook);
}

std::vector<SequencedEffect> UiMachine::Dispatch(const Input& input) {
  const std::uint64_t delivered = delivered_announcement_.load();
  std::erase_if(state_.announcements, [delivered](const Announcement& item) {
    return item.sequence <= delivered;
  });
  Step step = Reduce(state_, config_, NowMicros(), input);
  state_ = std::move(step.state);
  assert(CheckInvariants(state_, config_).empty());
  std::vector<SequencedEffect> effects;
  effects.reserve(step.effects.size());
  for (Effect& effect : step.effects) {
    effects.push_back(SequencedEffect{.sequence = next_effect_++,
                                      .effect = std::move(effect)});
  }
  std::shared_ptr<const ViewModel> latest;
  {
    const base::AutoLock lock(lock_);
    latest = latest_;
  }
  ViewModel baseline = *latest;
  std::erase_if(baseline.announcements.queue,
                [delivered](const Announcement& item) {
                  return item.sequence <= delivered;
                });
  ViewModel next = BuildViewModel(state_, config_, baseline);
  if (!VersionsDiffer(next, baseline)) {
    return effects;
  }
  {
    const base::AutoLock lock(lock_);
    latest_ = std::make_shared<const ViewModel>(std::move(next));
  }
  if (!frame_requested_.exchange(true) && frame_hook_) {
    frame_hook_();
  }
  return effects;
}

std::shared_ptr<const ViewModel> UiMachine::TakeSnapshot() {
  frame_requested_.store(false);
  std::shared_ptr<const ViewModel> snapshot;
  {
    const base::AutoLock lock(lock_);
    snapshot = latest_;
  }
  const auto& queue = snapshot->announcements.queue;
  if (!queue.empty()) {
    delivered_announcement_.store(queue.back().sequence);
  }
  return snapshot;
}

}
