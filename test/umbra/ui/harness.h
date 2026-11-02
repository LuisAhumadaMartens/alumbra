#ifndef TEST_UMBRA_UI_HARNESS_H_
#define TEST_UMBRA_UI_HARNESS_H_

#include <memory>
#include <vector>

#include "absl/time/time.h"
#include "umbra/base/test_support/fake_tick_clock.h"
#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/machine.h"
#include "umbra/ui/types.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {

class Harness {
 public:
  explicit Harness(UiConfig config = {});

  void Send(const Input& input);
  void Advance(absl::Duration delta);
  void Prepare(bool playing);
  void AdvanceMillis(int millis) { Advance(absl::Milliseconds(millis)); }

  [[nodiscard]] std::shared_ptr<const ViewModel> Snapshot() {
    return machine_.TakeSnapshot();
  }
  [[nodiscard]] const UiState& State() const { return machine_.State(); }
  [[nodiscard]] UiMachine& Machine() { return machine_; }
  [[nodiscard]] base::FakeTickClock& clock() { return clock_; }
  [[nodiscard]] const std::vector<SequencedEffect>& Effects() const {
    return effects_;
  }
  void ClearEffects() { effects_.clear(); }

 private:
  struct Pending {
    Micros due_us;
    TimerFired timer;
  };

  void Schedule(const std::vector<SequencedEffect>& effects);

  base::FakeTickClock clock_;
  UiMachine machine_;
  std::vector<SequencedEffect> effects_;
  std::vector<Pending> pending_;
};

}

#endif
