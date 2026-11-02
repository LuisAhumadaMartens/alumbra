#include "harness.h"

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

#include "absl/time/time.h"
#include "umbra/base/time_ticks.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/types.h"

namespace umbra::ui {

Harness::Harness(UiConfig config) : machine_(clock_, std::move(config)) {}

void Harness::Schedule(const std::vector<SequencedEffect>& effects) {
  const Micros now = machine_.NowMicros();
  for (const SequencedEffect& sequenced : effects) {
    if (const auto* timer = std::get_if<StartTimer>(&sequenced.effect)) {
      pending_.push_back(
          Pending{.due_us = now + timer->delay_us,
                  .timer = TimerFired{.kind = timer->kind,
                                      .generation = timer->generation}});
    }
  }
}

void Harness::Send(const Input& input) {
  std::vector<SequencedEffect> produced = machine_.Dispatch(input);
  Schedule(produced);
  effects_.insert(effects_.end(), produced.begin(), produced.end());
}

void Harness::Advance(absl::Duration delta) {
  const Micros target = machine_.NowMicros() + absl::ToInt64Microseconds(delta);
  for (;;) {
    const auto due = std::ranges::min_element(
        pending_, [](const Pending& lhs, const Pending& rhs) {
          return lhs.due_us < rhs.due_us;
        });
    if (due == pending_.end() || due->due_us > target) {
      break;
    }
    const Pending fired = *due;
    pending_.erase(due);
    clock_.SetNowTicks(base::TimeTicks::FromSinceOrigin(
        absl::Microseconds(std::max(fired.due_us, machine_.NowMicros()))));
    Send(fired.timer);
  }
  clock_.SetNowTicks(
      base::TimeTicks::FromSinceOrigin(absl::Microseconds(target)));
}

void Harness::Prepare(bool playing) {
  Send(LoadStarted{});
  Send(DurationChanged{.duration_us = 600'000'000, .live = false});
  Send(CanPlay{});
  if (playing) {
    Send(TogglePlay{});
    Send(PlaybackStarted{});
  }
}

}
