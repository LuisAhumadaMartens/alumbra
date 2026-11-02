#ifndef TEST_PENUMBRA_CONFORMANCE_PORT_HARNESS_H_
#define TEST_PENUMBRA_CONFORMANCE_PORT_HARNESS_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <string>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"

namespace penumbra::conformance {

enum class Capability {
  kDataRequestHook,
  kErrorInjection,
  kClockAdvance,
  kFragmentInput,
  kPunchOutOutput,
  kTextureOutput,
  kComponents,
  kVsyncDrive,
  kAudioRenderDrive,
};

std::string CapabilityName(Capability capability);

class PortHarness {
 public:
  virtual ~PortHarness() = default;
  virtual Port& UnderTest() = 0;
  virtual absl::Status RequestData(Player& player, uint32_t track_id,
                                   size_t max_samples);
  virtual absl::Status RequestDecoderData(Decoder& decoder, size_t count);
  virtual absl::Status InjectError(Player& player, const PortError& error);
  virtual absl::Status AdvancePlayback(Player& player, absl::Duration elapsed);
  virtual std::optional<DecodedVideoFrame> DriveVsync(VideoSink& sink,
                                                      MediaTime time);
  virtual size_t DriveAudioRender(AudioSink& sink,
                                  std::span<uint8_t> destination);
};

using PortFactory = std::function<std::unique_ptr<PortHarness>(
    std::shared_ptr<ControlTaskRunner> control)>;

struct PortCase {
  std::string name;
  PortFactory factory;
  std::set<Capability> capabilities;
};

void PrintTo(const PortCase& port_case, std::ostream* out);

std::string PortCaseName(const ::testing::TestParamInfo<PortCase>& info);

}

#endif
