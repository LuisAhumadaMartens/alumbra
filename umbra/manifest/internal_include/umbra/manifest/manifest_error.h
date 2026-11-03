#ifndef UMBRA_MANIFEST_MANIFEST_ERROR_H_
#define UMBRA_MANIFEST_MANIFEST_ERROR_H_

#include <string_view>

#include "absl/status/status.h"

namespace umbra::manifest {

[[nodiscard]] absl::Status InvalidManifest(std::string_view what);

}

#endif
