#include <cstddef>
#include <cstdint>
#include <string_view>

#include "absl/strings/cord.h"
#include "umbra/base/status.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  absl::Status status = absl::InternalError("fuzz");
  status.SetPayload(umbra::base::kErrorDetailPayloadUrl, absl::Cord(input));
  const auto detail = umbra::base::GetErrorDetail(status);
  const umbra::base::UrlRedactor redactor({1, 2});
  const auto url = redactor.RedactUrl(input);
  const auto message = redactor.RedactText(input);
  const auto field = redactor.RedactField("request_url", input);
  auto valid = umbra::base::MakeError(
      umbra::base::ErrorCode::kPlayerInternal, redactor,
      {{"http_status", "503"}, {"request_url", "https://host/path"}});
  std::string bytes =
      std::string(*valid.GetPayload(umbra::base::kErrorDetailPayloadUrl));
  for (std::size_t i = 0; i + 1 < size; i += 2) {
    bytes[data[i] % bytes.size()] = static_cast<char>(data[i + 1]);
  }
  valid.SetPayload(umbra::base::kErrorDetailPayloadUrl, absl::Cord(bytes));
  const auto mutated = umbra::base::GetErrorDetail(valid);
  if (detail.ok() && detail->has_value()) {
    const auto round_trip =
        umbra::base::AttachErrorDetail(status, **detail, redactor);
    const auto decoded = umbra::base::GetErrorDetail(round_trip);
    if (!decoded.ok() || !decoded->has_value()) {
      __builtin_trap();
    }
  }
  return 0;
}
