#ifndef PENUMBRA_APPLE_URL_SESSION_METRICS_H_
#define PENUMBRA_APPLE_URL_SESSION_METRICS_H_

#include <optional>
#include <string_view>

#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"

namespace penumbra::apple {

HttpProtocol ProtocolFromName(std::string_view name);
std::optional<absl::Duration> MapSessionPhase(std::optional<double> start,
                                              std::optional<double> end);

}

#endif
