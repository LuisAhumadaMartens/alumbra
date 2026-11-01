#ifndef UMBRA_BASE_ERROR_DETAIL_H_
#define UMBRA_BASE_ERROR_DETAIL_H_

#include <map>
#include <memory>
#include <string>

#include "umbra/base/error_catalogue.h"

namespace umbra::base {

struct ErrorDetail {
  ErrorCode code = ErrorCode::kPlayerInternal;
  ErrorCategory category = ErrorCategory::kPlayer;
  ErrorSeverity severity = ErrorSeverity::kFatal;
  bool retryable = false;
  std::map<std::string, std::string> context;
  std::shared_ptr<const ErrorDetail> cause;
};

}

#endif
