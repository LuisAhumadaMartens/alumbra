#ifndef PENUMBRA_DESKTOP_DECODE_ERRORS_H_
#define PENUMBRA_DESKTOP_DECODE_ERRORS_H_

#include <string>
#include <string_view>

#include "absl/status/status.h"

namespace penumbra::desktop {

[[nodiscard]] absl::Status CorruptSampleError(std::string_view what);
[[nodiscard]] absl::Status CodecUnsupportedError(std::string_view what);
[[nodiscard]] absl::Status DecoderInitializationError(std::string_view what);
[[nodiscard]] absl::Status DecodeFailedError(std::string_view what);

[[nodiscard]] std::string AvErrorText(int code);
[[nodiscard]] absl::Status StatusFromAvError(int code,
                                             std::string_view operation);

}

#endif
