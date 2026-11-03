#ifndef UMBRA_MANIFEST_REFRESH_H_
#define UMBRA_MANIFEST_REFRESH_H_

#include <memory>

#include "absl/status/statusor.h"
#include "umbra/manifest/presentation.h"

namespace umbra::manifest {

[[nodiscard]] absl::StatusOr<std::shared_ptr<const Presentation>>
RefreshPresentation(const std::shared_ptr<const Presentation>& previous,
                    Presentation next);

}

#endif
