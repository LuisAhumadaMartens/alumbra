#ifndef UMBRA_SOURCE_SOURCE_ERROR_H_
#define UMBRA_SOURCE_SOURCE_ERROR_H_

#include <map>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/url_redactor.h"

namespace umbra::source {

[[nodiscard]] absl::Status MakeSourceError(
    base::ErrorCode code, absl::StatusCode status_code,
    std::string_view message, std::map<std::string, std::string> context,
    const base::UrlRedactor& redactor);

}

#endif
