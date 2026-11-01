#ifndef UMBRA_BASE_STATUS_H_
#define UMBRA_BASE_STATUS_H_

#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/url_redactor.h"

namespace umbra::base {

inline constexpr char kErrorDetailPayloadUrl[] = "alumbra.dev/ErrorDetail/v1";

[[nodiscard]] absl::Status MakeError(
    ErrorCode code, const UrlRedactor& redactor,
    std::map<std::string, std::string> context = {},
    std::shared_ptr<const ErrorDetail> cause = {});

[[nodiscard]] absl::Status AttachErrorDetail(absl::Status status,
                                             const ErrorDetail& detail,
                                             const UrlRedactor& redactor);

[[nodiscard]] absl::StatusOr<std::optional<ErrorDetail>> GetErrorDetail(
    const absl::Status& status);

}

#define ALUMBRA_STATUS_CONCAT_INNER_(x, y) x##y
#define ALUMBRA_STATUS_CONCAT_(x, y) ALUMBRA_STATUS_CONCAT_INNER_(x, y)

#define ALUMBRA_RETURN_IF_ERROR(expression)                   \
  do {                                                        \
    const absl::Status alumbra_return_status_ = (expression); \
    if (!alumbra_return_status_.ok()) {                       \
      return alumbra_return_status_;                          \
    }                                                         \
  } while (false)

#define ALUMBRA_ASSIGN_OR_RETURN_IMPL_(temporary, lhs, expression) \
  auto temporary = (expression);                                   \
  if (!temporary.ok()) {                                           \
    return std::move(temporary).status();                          \
  }                                                                \
  lhs = *std::move(temporary)

#define ALUMBRA_ASSIGN_OR_RETURN(lhs, expression)                   \
  ALUMBRA_ASSIGN_OR_RETURN_IMPL_(                                   \
      ALUMBRA_STATUS_CONCAT_(alumbra_status_or_, __COUNTER__), lhs, \
      expression)

#endif
