#include <memory>
#include <utility>
#include <vector>

#include "penumbra/desktop/desktop_port.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::desktop::test {
namespace {

class DesktopHarness final : public conformance::PortHarness {
 public:
  explicit DesktopHarness(std::shared_ptr<ControlTaskRunner> control) {
    auto result = CreatePort({.headless = true,
                              .window_title = "Alumbra",
                              .window_width = 1280,
                              .window_height = 720,
                              .display_rate_match = DisplayRateMatch::kSeamless,
                              .audio_output_device = {},
                              .video_threads = 0,
                              .av1_apply_film_grain = true,
                              .av1_max_frame_delay = 0,
                              .av1_operating_point = 0,
                              .disabled_decoders = {},
                              .max_video_height = 0,
                              .decoder_settings = {},
                              .http_connect_timeout_ms = 10000,
                              .http_read_timeout_ms = 20000,
                              .http_max_redirects = 20,
                              .http_user_agent = {},
                              .http_allow_https_to_http_redirect = false,
                              .buffer_budgets = {},
                              .ca_bundle_path = {},
                              .audio_sink_factory = {},
                              .video_sink_factory = {}});
    if (result.ok()) {
      port_ = std::move(*result);
      static_cast<void>(
          SetControlTaskRunnerForTesting(*port_, std::move(control)));
    }
  }

  Port& UnderTest() override { return *port_; }

 private:
  std::unique_ptr<Port> port_;
};

const std::vector<conformance::PortCase>& Cases() {
  static const std::vector<conformance::PortCase> cases = {
      {.name = "desktop",
       .factory =
           [](std::shared_ptr<ControlTaskRunner> control) {
             return std::make_unique<DesktopHarness>(std::move(control));
           },
       .capabilities = {conformance::Capability::kTextureOutput,
                        conformance::Capability::kComponents}}};
  return cases;
}

}

ALUMBRA_INSTANTIATE_PORT_CONFORMANCE(DesktopConformance, Cases());

}
