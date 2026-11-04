#ifndef UMBRA_MANIFEST_REFRESH_H_
#define UMBRA_MANIFEST_REFRESH_H_

#include <functional>
#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

[[nodiscard]] absl::StatusOr<std::shared_ptr<const Presentation>>
RefreshPresentation(
    const std::shared_ptr<const Presentation>& previous, Presentation next,
    const std::function<void(std::string_view)>& warning_sink = {});

}

#endif
