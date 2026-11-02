#ifndef PENUMBRA_REFERENCE_PLAYER_DROP_ATTRIBUTION_H_
#define PENUMBRA_REFERENCE_PLAYER_DROP_ATTRIBUTION_H_

#include <cstdint>
#include <optional>

#include "penumbra/interface/media_time.h"

namespace penumbra {

enum class DropCause : uint8_t { kDecodeLate, kPresentLate };

struct DropTriggerEvent {
  uint64_t decode_late = 0;
  uint64_t frames_due = 0;
  int32_t max_percent = 0;
  TimeDelta window;
};

class DropAttribution {
 public:
  DropAttribution(int32_t max_percent, TimeDelta window);

  void Reconfigure(int32_t max_percent, TimeDelta window);
  void RestartWindow();
  std::optional<DropTriggerEvent> RecordFrameDue(TimeDelta now);
  std::optional<DropTriggerEvent> RecordDrop(DropCause cause, TimeDelta now);
  std::optional<DropTriggerEvent> Poll(TimeDelta now);

  [[nodiscard]] uint64_t DecodeLate() const { return decode_late_total_; }
  [[nodiscard]] uint64_t PresentLate() const { return present_late_total_; }
  [[nodiscard]] uint64_t Triggers() const { return triggers_; }

 private:
  std::optional<DropTriggerEvent> Advance(TimeDelta now);

  int32_t max_percent_;
  TimeDelta window_;
  TimeDelta window_start_ = TimeDelta::Unknown();
  uint64_t window_due_ = 0;
  uint64_t window_decode_late_ = 0;
  uint64_t decode_late_total_ = 0;
  uint64_t present_late_total_ = 0;
  uint64_t triggers_ = 0;
};

}

#endif
