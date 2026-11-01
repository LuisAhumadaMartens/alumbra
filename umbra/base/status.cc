#include "umbra/base/status.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/cord.h"

namespace umbra::base {
namespace {

constexpr std::size_t kMaxPayloadBytes = 65536;
constexpr std::size_t kMaxCauseDepth = 16;
constexpr std::size_t kMaxContextEntries = 64;

[[nodiscard]] absl::Status InvalidDetail() {
  return absl::InvalidArgumentError("Invalid Alumbra error detail");
}

void WriteNumber(std::uint32_t value, std::string& output) {
  for (int i = 0; i < 4; ++i) {
    output += static_cast<char>((value >> (i * 8)) & 255);
  }
}

bool ReadNumber(std::string_view& input, std::uint32_t& value) {
  if (input.size() < 4) {
    return false;
  }
  value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[i]))
             << (8 * i);
  }
  input.remove_prefix(4);
  return true;
}

void WriteString(std::string_view value, std::string& output) {
  WriteNumber(static_cast<std::uint32_t>(value.size()), output);
  output += value;
}

bool ReadString(std::string_view& input, std::string& value) {
  std::uint32_t length = 0;
  if (!ReadNumber(input, length) || length > input.size()) {
    return false;
  }
  value.assign(input.substr(0, length));
  input.remove_prefix(length);
  return true;
}

[[nodiscard]] absl::Status Encode(const ErrorDetail& detail,
                                  const UrlRedactor& redactor,
                                  std::size_t depth, std::string& output) {
  const auto* entry = FindError(detail.code);
  if (!entry || entry->category != detail.category ||
      detail.severity < ErrorSeverity::kRecoverable ||
      detail.severity > ErrorSeverity::kFatal || depth >= kMaxCauseDepth ||
      detail.context.size() > kMaxContextEntries) {
    return InvalidDetail();
  }
  WriteNumber(static_cast<std::uint32_t>(detail.code), output);
  WriteNumber(static_cast<std::uint32_t>(detail.category), output);
  WriteNumber(static_cast<std::uint32_t>(detail.severity), output);
  WriteNumber(detail.retryable, output);
  WriteNumber(static_cast<std::uint32_t>(detail.context.size()), output);
  for (const auto& [key, value] : detail.context) {
    if (key.empty() || key.size() > 128 ||
        key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.") !=
            std::string::npos ||
        value.size() > kMaxPayloadBytes) {
      return InvalidDetail();
    }
    WriteString(key, output);
    WriteString(redactor.RedactField(key, value), output);
    if (output.size() > kMaxPayloadBytes) {
      return InvalidDetail();
    }
  }
  WriteNumber(detail.cause != nullptr, output);
  if (detail.cause) {
    ALUMBRA_RETURN_IF_ERROR(Encode(*detail.cause, redactor, depth + 1, output));
  }
  return output.size() <= kMaxPayloadBytes ? absl::OkStatus() : InvalidDetail();
}

[[nodiscard]] absl::StatusOr<ErrorDetail> Decode(std::string_view& input,
                                                 std::size_t depth) {
  std::uint32_t code = 0, category = 0, severity = 0, retryable = 0, count = 0;
  if (depth >= kMaxCauseDepth || !ReadNumber(input, code) ||
      !ReadNumber(input, category) || !ReadNumber(input, severity) ||
      !ReadNumber(input, retryable) || !ReadNumber(input, count) ||
      severity > static_cast<std::uint32_t>(ErrorSeverity::kFatal) ||
      retryable > 1 || count > kMaxContextEntries) {
    return InvalidDetail();
  }
  const auto* entry = FindError(static_cast<ErrorCode>(code));
  if (!entry || category != static_cast<std::uint32_t>(entry->category)) {
    return InvalidDetail();
  }
  ErrorDetail result{entry->code,
                     entry->category,
                     static_cast<ErrorSeverity>(severity),
                     retryable != 0,
                     {},
                     {}};
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string key, value;
    if (!ReadString(input, key) || !ReadString(input, value) || key.empty() ||
        key.size() > 128 ||
        key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.") !=
            std::string::npos ||
        !result.context.emplace(std::move(key), std::move(value)).second) {
      return InvalidDetail();
    }
  }
  std::uint32_t cause = 0;
  if (!ReadNumber(input, cause) || cause > 1) {
    return InvalidDetail();
  }
  if (cause) {
    ALUMBRA_ASSIGN_OR_RETURN(auto nested, Decode(input, depth + 1));
    result.cause = std::make_shared<const ErrorDetail>(std::move(nested));
  }
  return result;
}

absl::StatusCode Canonical(std::string_view name) {
  for (int i = 1; i <= 16; ++i) {
    const auto code = static_cast<absl::StatusCode>(i);
    std::string camel;
    bool upper = true;
    for (char ch : absl::StatusCodeToString(code)) {
      if (ch == '_') {
        upper = true;
      } else {
        camel += upper ? ch : static_cast<char>(ch + 'a' - 'A');
        upper = false;
      }
    }
    if (camel == name) {
      return code;
    }
  }
  return absl::StatusCode::kUnknown;
}

}

absl::Status MakeError(ErrorCode code, const UrlRedactor& redactor,
                       std::map<std::string, std::string> context,
                       std::shared_ptr<const ErrorDetail> cause) {
  const auto* entry = FindError(code);
  if (!entry) {
    return InvalidDetail();
  }
  ErrorDetail detail{code,
                     entry->category,
                     entry->severity,
                     entry->retryable,
                     std::move(context),
                     std::move(cause)};
  absl::Status status(Canonical(entry->canonical),
                      "ALUMBRA-" +
                          std::to_string(static_cast<std::uint32_t>(code)) +
                          ": " + std::string(entry->description));
  return AttachErrorDetail(std::move(status), detail, redactor);
}

absl::Status AttachErrorDetail(absl::Status status, const ErrorDetail& detail,
                               const UrlRedactor& redactor) {
  if (status.ok()) {
    return absl::InvalidArgumentError("Cannot attach error detail to OK");
  }
  std::string payload;
  ALUMBRA_RETURN_IF_ERROR(Encode(detail, redactor, 0, payload));
  absl::Status sanitized(status.code(), redactor.RedactText(status.message()));
  status.ForEachPayload([&](absl::string_view type, const absl::Cord& value) {
    if (type != kErrorDetailPayloadUrl) {
      sanitized.SetPayload(type, value);
    }
  });
  sanitized.SetPayload(kErrorDetailPayloadUrl, absl::Cord(payload));
  return sanitized;
}

absl::StatusOr<std::optional<ErrorDetail>> GetErrorDetail(
    const absl::Status& status) {
  const auto payload = status.GetPayload(kErrorDetailPayloadUrl);
  if (!payload) {
    return std::nullopt;
  }
  if (payload->size() < 24 || payload->size() > kMaxPayloadBytes) {
    return InvalidDetail();
  }
  const std::string bytes = std::string(*payload);
  std::string_view input(bytes);
  ALUMBRA_ASSIGN_OR_RETURN(auto detail, Decode(input, 0));
  if (!input.empty()) {
    return InvalidDetail();
  }
  return std::optional<ErrorDetail>(std::move(detail));
}

}
