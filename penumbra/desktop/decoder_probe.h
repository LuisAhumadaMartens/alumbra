#ifndef PENUMBRA_DESKTOP_DECODER_PROBE_H_
#define PENUMBRA_DESKTOP_DECODER_PROBE_H_

#include <string_view>

#include "absl/status/statusor.h"

namespace penumbra::desktop {

class DecoderProbe {
 public:
  virtual ~DecoderProbe() = default;
  [[nodiscard]] virtual absl::StatusOr<bool> IsCompiledIn(
      std::string_view decoder_name) const = 0;
};

}

#endif
