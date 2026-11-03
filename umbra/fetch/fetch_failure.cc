#include "umbra/fetch/fetch_failure.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"

namespace umbra::fetch {

namespace {

constexpr std::string_view kPayloadType = "alumbra.dev/FetchFailure/v1";
constexpr int64_t kMaxRetryAfterSeconds = 86400;
constexpr size_t kMaxRetryAfterDigits = 9;

int ClassCode(penumbra::RequestClass request_class) {
  return static_cast<int>(request_class);
}

}

std::optional<absl::Duration> ParseRetryAfter(
    const std::vector<penumbra::HttpHeader>& headers) {
  for (const penumbra::HttpHeader& header : headers) {
    if (!absl::EqualsIgnoreCase(header.name, "retry-after")) {
      continue;
    }
    const absl::string_view value = absl::StripAsciiWhitespace(header.value);
    if (value.empty() || value.size() > kMaxRetryAfterDigits) {
      return std::nullopt;
    }
    for (const char c : value) {
      if (c < '0' || c > '9') {
        return std::nullopt;
      }
    }
    int64_t seconds = 0;
    if (!absl::SimpleAtoi(value, &seconds) || seconds > kMaxRetryAfterSeconds) {
      return std::nullopt;
    }
    return absl::Seconds(seconds);
  }
  return std::nullopt;
}

absl::Status AttachFetchFailure(absl::Status status,
                                const FetchFailure& failure) {
  if (status.ok()) {
    return status;
  }
  const int64_t retry_after_ms =
      failure.retry_after.has_value()
          ? absl::ToInt64Milliseconds(*failure.retry_after)
          : -1;
  status.SetPayload(kPayloadType,
                    absl::Cord(absl::StrCat(
                        failure.http_status, "|", retry_after_ms, "|",
                        ClassCode(failure.request_class), "|", failure.url)));
  return status;
}

std::optional<FetchFailure> GetFetchFailure(const absl::Status& status) {
  const std::optional<absl::Cord> payload = status.GetPayload(kPayloadType);
  if (!payload.has_value()) {
    return std::nullopt;
  }
  const std::string text(*payload);
  std::string_view rest = text;
  std::array<std::string, 3> fields;
  for (std::string& field : fields) {
    const size_t bar = rest.find('|');
    if (bar == std::string_view::npos) {
      return std::nullopt;
    }
    field = std::string(rest.substr(0, bar));
    rest.remove_prefix(bar + 1);
  }
  int http_status = 0;
  int64_t retry_after_ms = 0;
  int class_code = 0;
  if (!absl::SimpleAtoi(fields[0], &http_status) ||
      !absl::SimpleAtoi(fields[1], &retry_after_ms) ||
      !absl::SimpleAtoi(fields[2], &class_code) || class_code < 0 ||
      class_code > ClassCode(penumbra::RequestClass::kPrefetch)) {
    return std::nullopt;
  }
  FetchFailure failure;
  failure.url = std::string(rest);
  failure.request_class = static_cast<penumbra::RequestClass>(class_code);
  failure.http_status = http_status;
  if (retry_after_ms >= 0) {
    failure.retry_after = absl::Milliseconds(retry_after_ms);
  }
  return failure;
}

}
