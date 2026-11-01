#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "fuzz_target.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::span<const std::uint8_t> raw(data, size);
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  absl::Status status = absl::InternalError("fuzz");
  status.SetPayload(umbra::base::kErrorDetailPayloadUrl, absl::Cord(input));
  const auto detail = umbra::base::GetErrorDetail(status);
  const umbra::base::UrlRedactor redactor({1, 2});
  static_cast<void>(redactor.RedactUrl(input));
  static_cast<void>(redactor.RedactText(input));
  static_cast<void>(redactor.RedactField("request_url", input));
  auto valid = umbra::base::MakeError(
      umbra::base::ErrorCode::kPlayerInternal, redactor,
      {{"http_status", "503"}, {"request_url", "https://host/path"}});
  std::string bytes =
      std::string(*valid.GetPayload(umbra::base::kErrorDetailPayloadUrl));
  for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
    bytes[raw[i] % bytes.size()] = static_cast<char>(raw[i + 1]);
  }
  valid.SetPayload(umbra::base::kErrorDetailPayloadUrl, absl::Cord(bytes));
  const auto mutated = umbra::base::GetErrorDetail(valid);
  if (mutated.ok() && mutated->has_value()) {
    const auto sanitized =
        umbra::base::AttachErrorDetail(valid, **mutated, redactor);
    if (sanitized.code() != absl::StatusCode::kInvalidArgument &&
        !umbra::base::GetErrorDetail(sanitized).ok()) {
      std::abort();
    }
  }
  if (detail.ok() && detail->has_value()) {
    const auto round_trip =
        umbra::base::AttachErrorDetail(status, **detail, redactor);
    if (round_trip.code() == absl::StatusCode::kInvalidArgument) {
      return 0;
    }
    const auto decoded = umbra::base::GetErrorDetail(round_trip);
    if (!decoded.ok() || !decoded->has_value()) {
      std::abort();
    }
  }
  return 0;
}
