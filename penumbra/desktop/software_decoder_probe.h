#ifndef PENUMBRA_DESKTOP_SOFTWARE_DECODER_PROBE_H_
#define PENUMBRA_DESKTOP_SOFTWARE_DECODER_PROBE_H_

#include <string_view>

#include "absl/status/statusor.h"
#include "penumbra/desktop/decoder_probe.h"

namespace penumbra::desktop {

class SoftwareDecoderProbe : public DecoderProbe {
 public:
  [[nodiscard]] absl::StatusOr<bool> IsCompiledIn(
      std::string_view decoder_name) const override;
};

}

#endif
