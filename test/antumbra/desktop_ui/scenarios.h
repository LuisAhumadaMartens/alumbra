#ifndef TEST_ANTUMBRA_DESKTOP_UI_SCENARIOS_H_
#define TEST_ANTUMBRA_DESKTOP_UI_SCENARIOS_H_

#include "harness.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/ui/input.h"

namespace alumbra::desktop_ui::testing {

inline void Start(umbra::ui::Harness& harness) {
  harness.Prepare(true);
  harness.Send(
      umbra::ui::SeekableChanged{.start_us = 0, .end_us = 600'000'000});
  harness.Send(umbra::ui::BufferedChanged{
      .ranges = {umbra::ui::TimeRange{.start_us = 0, .end_us = 330'000'000}}});
  harness.Send(umbra::ui::VolumeChanged{.volume = 0.7, .muted = false});
  harness.Send(umbra::ui::TimeUpdate{
      .position_us = 228'000'000, .anchor_us = harness.Machine().NowMicros()});
}

inline void Playing(umbra::ui::Harness& harness) { Start(harness); }

inline void Paused(umbra::ui::Harness& harness) {
  Start(harness);
  harness.Send(umbra::ui::TogglePlay{});
  harness.Send(umbra::ui::PlaybackPaused{});
}

inline void Buffering(umbra::ui::Harness& harness) {
  Start(harness);
  harness.Send(umbra::ui::Waiting{});
  harness.AdvanceMillis(600);
}

inline void Failed(umbra::ui::Harness& harness) {
  Start(harness);
  harness.Send(umbra::ui::PlaybackFailed{
      .code = umbra::base::ErrorCode::kNetworkTimeout});
}

inline void ControlsHidden(umbra::ui::Harness& harness) {
  Start(harness);
  harness.AdvanceMillis(3500);
}

inline void Focused(umbra::ui::Harness& harness) {
  Paused(harness);
  harness.Send(umbra::ui::FocusChrome{});
}

inline void FocusedVolume(umbra::ui::Harness& harness) {
  Paused(harness);
  harness.Send(umbra::ui::FocusControl{.id = umbra::ui::ControlId::kVolume});
}

}

#endif
