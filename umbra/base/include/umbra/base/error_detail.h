#ifndef UMBRA_BASE_ERROR_DETAIL_H_
#define UMBRA_BASE_ERROR_DETAIL_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "umbra/base/error_catalogue.h"

namespace umbra::base {

namespace internal {
class ErrorDetailAccess;
struct ErrorDetailProvenance;
}

struct ErrorDetail {
  ErrorDetail() = default;
  ErrorDetail(ErrorCode code, ErrorCategory category, ErrorSeverity severity,
              bool retryable, std::map<std::string, std::string> context,
              std::shared_ptr<const ErrorDetail> cause)
      : code(code),
        category(category),
        severity(severity),
        retryable(retryable),
        context(std::move(context)),
        cause(std::move(cause)) {}

  ErrorCode code = ErrorCode::kPlayerInternal;
  ErrorCategory category = ErrorCategory::kPlayer;
  ErrorSeverity severity = ErrorSeverity::kFatal;
  bool retryable = false;
  std::map<std::string, std::string> context;
  std::shared_ptr<const ErrorDetail> cause;

 private:
  friend class internal::ErrorDetailAccess;
  std::shared_ptr<const internal::ErrorDetailProvenance> provenance_;
};

}

#endif
