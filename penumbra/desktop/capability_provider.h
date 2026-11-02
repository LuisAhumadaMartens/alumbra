#ifndef PENUMBRA_DESKTOP_CAPABILITY_PROVIDER_H_
#define PENUMBRA_DESKTOP_CAPABILITY_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::desktop {

struct DecodeLimits {
  std::vector<std::string> disabled_decoders;
  int32_t max_video_height = 0;
};

class DesktopCapabilityProvider : public CapabilityProvider {
 public:
  DesktopCapabilityProvider(DecodeLimits limits,
                            std::shared_ptr<const DecoderProbe> probe);

  absl::StatusOr<DecodingCapability> QueryDecoding(
      const CapabilityRequest& request) const override;
  absl::StatusOr<bool> CanChangeType(const StreamConfig& from,
                                     const StreamConfig& to) const override;

 private:
  absl::StatusOr<DecodingCapability> Evaluate(
      const CapabilityRequest& request) const;
  bool IsDisabled(std::string_view settings_name) const;

  DecodeLimits limits_;
  std::shared_ptr<const DecoderProbe> probe_;
  mutable std::mutex mutex_;
  mutable std::unordered_map<std::string, DecodingCapability> cache_;
};

}

#endif
