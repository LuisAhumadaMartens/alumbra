#ifndef PENUMBRA_INTERFACE_DEFAULT_PORT_H_
#define PENUMBRA_INTERFACE_DEFAULT_PORT_H_

#include <memory>

#include "absl/status/statusor.h"
#include "penumbra/interface/port.h"

namespace penumbra {

[[nodiscard]] absl::StatusOr<std::unique_ptr<Port>> CreateDefaultPort(
    bool headless);

}

#endif
