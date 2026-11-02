#include "penumbra/reference_player/drop_attribution.h"

#include <cstdint>
#include <optional>

#include "penumbra/interface/media_time.h"

namespace penumbra {

DropAttribution::DropAttribution(int32_t max_percent, TimeDelta window)
    : max_percent_(max_percent), window_(window) {}

void DropAttribution::Reconfigure(int32_t max_percent, TimeDelta window) {
  max_percent_ = max_percent;
  window_ = window;
}

void DropAttribution::RestartWindow() {
  window_start_ = TimeDelta::Unknown();
  window_due_ = 0;
  window_decode_late_ = 0;
}

std::optional<DropTriggerEvent> DropAttribution::Advance(TimeDelta now) {
  if (window_start_.is_unknown()) {
    window_start_ = now;
    return std::nullopt;
  }
  if (!window_.is_finite() || window_ <= TimeDelta::Zero() ||
      now - window_start_ < window_) {
    return std::nullopt;
  }
  std::optional<DropTriggerEvent> event;
  if (window_due_ > 0 &&
      window_decode_late_ * 100 >
          window_due_ *
              static_cast<uint64_t>(max_percent_ < 0 ? 0 : max_percent_)) {
    event = DropTriggerEvent{.decode_late = window_decode_late_,
                             .frames_due = window_due_,
                             .max_percent = max_percent_,
                             .window = window_};
    ++triggers_;
  }
  window_start_ = now;
  window_due_ = 0;
  window_decode_late_ = 0;
  return event;
}

std::optional<DropTriggerEvent> DropAttribution::RecordFrameDue(TimeDelta now) {
  auto event = Advance(now);
  ++window_due_;
  return event;
}

std::optional<DropTriggerEvent> DropAttribution::RecordDrop(DropCause cause,
                                                            TimeDelta now) {
  auto event = Advance(now);
  if (cause == DropCause::kDecodeLate) {
    ++window_decode_late_;
    ++decode_late_total_;
  } else {
    ++present_late_total_;
  }
  return event;
}

std::optional<DropTriggerEvent> DropAttribution::Poll(TimeDelta now) {
  return Advance(now);
}

}
