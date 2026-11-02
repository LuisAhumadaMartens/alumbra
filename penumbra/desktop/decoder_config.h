#ifndef PENUMBRA_DESKTOP_DECODER_CONFIG_H_
#define PENUMBRA_DESKTOP_DECODER_CONFIG_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

[[nodiscard]] absl::StatusOr<std::vector<uint8_t>> FfmpegExtradata(
    const StreamInfo& info);

}

#endif
