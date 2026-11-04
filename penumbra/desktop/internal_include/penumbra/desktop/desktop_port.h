#ifndef PENUMBRA_DESKTOP_DESKTOP_PORT_H_
#define PENUMBRA_DESKTOP_DESKTOP_PORT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/desktop/desktop_window_control.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra::desktop {

class AvSyncCapture;

enum class DisplayRateMatch { kOff, kSeamless, kAlways };

struct DesktopPortOptions {
  bool headless = false;
  std::shared_ptr<AvSyncCapture> av_sync_capture;
  DisplayRateMatch display_rate_match = DisplayRateMatch::kSeamless;
  std::string audio_output_device;
  int32_t video_threads = 0;
  bool av1_apply_film_grain = true;
  int32_t av1_max_frame_delay = 0;
  int32_t av1_operating_point = 0;
  std::vector<std::string> disabled_decoders;
  int32_t max_video_height = 0;
  DecoderSettings decoder_settings;
  int64_t http_connect_timeout_ms = 10000;
  int64_t http_read_timeout_ms = 20000;
  int32_t http_max_redirects = 20;
  std::string http_user_agent;
  bool http_allow_https_to_http_redirect = false;
  BufferBudgets buffer_budgets;
  std::optional<std::string> ca_bundle_path;
  std::function<absl::StatusOr<std::unique_ptr<AudioSink>>()>
      audio_sink_factory;
  std::function<absl::StatusOr<std::unique_ptr<VideoSink>>(OutputMode)>
      video_sink_factory;
};

[[nodiscard]] absl::StatusOr<std::unique_ptr<Port>> CreatePort(
    DesktopPortOptions options = {});
[[nodiscard]] DesktopWindowControl* GetDesktopWindowControl(Port& port);

absl::Status SetControlTaskRunnerForTesting(
    Port& port, std::shared_ptr<ControlTaskRunner> control);
void SetDefaultAudioSinkFactoryForTesting(
    std::function<absl::StatusOr<std::unique_ptr<AudioSink>>()> factory);
void SetDefaultVideoSinkFactoryForTesting(
    std::function<absl::StatusOr<std::unique_ptr<VideoSink>>(OutputMode)>
        factory);

}

#endif
