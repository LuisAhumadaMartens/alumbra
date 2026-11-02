#ifndef UMBRA_SOURCE_FILE_URL_H_
#define UMBRA_SOURCE_FILE_URL_H_

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace umbra::source {

[[nodiscard]] absl::StatusOr<std::string> FileUrlFromPath(
    std::string_view absolute_path);

}

#endif
