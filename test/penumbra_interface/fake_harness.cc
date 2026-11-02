#include "fake_harness.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "fake_port.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::test {
namespace {

using conformance::Capability;
using conformance::PortCase;
using conformance::PortHarness;

class FakeHarness final : public PortHarness {
 public:
  FakeHarness(std::shared_ptr<ControlTaskRunner> control, FakeFaults faults)
      : port_(std::move(control), faults) {}

  Port& UnderTest() override { return port_; }

  absl::Status RequestData(Player& player, uint32_t track_id,
                           size_t max_samples) override {
    auto* fake = dynamic_cast<FakePlayer*>(&player);
    return fake == nullptr ? NotFake()
                           : fake->RequestData(track_id, max_samples);
  }

  absl::Status RequestDecoderData(Decoder& decoder, size_t count) override {
    auto* fake = dynamic_cast<FakeDecoder*>(&decoder);
    return fake == nullptr ? NotFake() : fake->RequestData(count);
  }

  absl::Status InjectError(Player& player, const PortError& error) override {
    auto* fake = dynamic_cast<FakePlayer*>(&player);
    return fake == nullptr ? NotFake() : fake->ReportError(error);
  }

  absl::Status AdvancePlayback(Player& player,
                               absl::Duration elapsed) override {
    auto* fake = dynamic_cast<FakePlayer*>(&player);
    return fake == nullptr ? NotFake() : fake->Advance(elapsed);
  }

  std::optional<DecodedVideoFrame> DriveVsync(VideoSink& sink,
                                              MediaTime time) override {
    auto* fake = dynamic_cast<FakeVideoSink*>(&sink);
    return fake == nullptr ? std::nullopt : fake->Vsync(time);
  }

  size_t DriveAudioRender(AudioSink& sink,
                          std::span<uint8_t> destination) override {
    auto* fake = dynamic_cast<FakeAudioSink*>(&sink);
    return fake == nullptr ? 0 : fake->Render(destination);
  }

 private:
  static absl::Status NotFake() {
    return absl::InternalError("object was not created by the fake port");
  }

  FakePort port_;
};

PortCase MakeCase(std::string name, FakeFaults faults) {
  return {
      .name = std::move(name),
      .factory =
          [faults](std::shared_ptr<ControlTaskRunner> control) {
            return std::make_unique<FakeHarness>(std::move(control), faults);
          },
      .capabilities = {Capability::kDataRequestHook,
                       Capability::kErrorInjection, Capability::kClockAdvance,
                       Capability::kFragmentInput, Capability::kPunchOutOutput,
                       Capability::kTextureOutput, Capability::kComponents,
                       Capability::kVsyncDrive, Capability::kAudioRenderDrive}};
}

}

std::vector<PortCase> FakePortCases() {
  std::vector<PortCase> cases;
  cases.push_back(MakeCase("fake", {}));
  return cases;
}

std::vector<PortCase> BrokenFakePortCases() {
  std::vector<PortCase> cases;
  cases.push_back(MakeCase("broken_reentrant_callback",
                           {.deliver_outside_callback_scope = true}));
  cases.push_back(
      MakeCase("broken_ignored_seek_ticket", {.ignore_seek_ticket = true}));
  cases.push_back(MakeCase("broken_non_monotonic_audio_position",
                           {.regress_media_time = true}));
  return cases;
}

}
