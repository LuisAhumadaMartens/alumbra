#include "umbra/settings/setting_value.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {
namespace {

constexpr std::size_t kMaxShownValueBytes = 64;
constexpr int kShortPrecision = 6;
constexpr int kRoundTripPrecision = 17;

bool IsIntegerLike(ValueType type) {
  return type == ValueType::kInteger || type == ValueType::kDuration ||
         type == ValueType::kByteSize;
}

bool Contains(const SettingInfo& info, std::string_view value) {
  return ListIndexOf(info.allowed, value).has_value();
}

std::string_view TypeName(ValueType type) {
  switch (type) {
    case ValueType::kBoolean:
      return "a boolean";
    case ValueType::kInteger:
    case ValueType::kDuration:
    case ValueType::kByteSize:
      return "an integer";
    case ValueType::kDouble:
      return "a number";
    case ValueType::kEnum:
    case ValueType::kString:
      return "a string";
    case ValueType::kStringList:
      return "a string list";
  }
  return "a value";
}

bool HoldsType(ValueType type, const Value& value) {
  switch (type) {
    case ValueType::kBoolean:
      return std::holds_alternative<bool>(value);
    case ValueType::kInteger:
    case ValueType::kDuration:
    case ValueType::kByteSize:
      return std::holds_alternative<std::int64_t>(value);
    case ValueType::kDouble:
      return std::holds_alternative<double>(value);
    case ValueType::kEnum:
    case ValueType::kString:
      return std::holds_alternative<std::string>(value);
    case ValueType::kStringList:
      return std::holds_alternative<std::vector<std::string>>(value);
  }
  return false;
}

std::vector<std::string> Elements(std::string_view list) {
  std::vector<std::string> elements;
  const std::size_t size = ListSize(list);
  elements.reserve(size);
  for (std::size_t index = 0; index < size; ++index) {
    elements.emplace_back(ListAt(list, index));
  }
  return elements;
}

std::string FormatDouble(double value, int precision) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream.precision(precision);
  stream << value;
  return stream.str();
}

std::string Quote(std::string_view value) {
  return absl::StrCat("\"", value, "\"");
}

}

std::optional<SettingLookup> FindSetting(const Registry& registry,
                                         std::string_view name) {
  const auto setting =
      std::ranges::lower_bound(registry.settings, name, {}, &SettingInfo::name);
  if (setting != registry.settings.end() && setting->name == name) {
    return SettingLookup{
        .index = static_cast<std::size_t>(setting - registry.settings.begin()),
        .via_alias = false};
  }
  const auto alias =
      std::ranges::lower_bound(registry.aliases, name, {}, &AliasInfo::name);
  if (alias != registry.aliases.end() && alias->name == name) {
    return SettingLookup{.index = alias->index, .via_alias = true};
  }
  return std::nullopt;
}

Value DefaultValue(const SettingInfo& info) {
  switch (info.type) {
    case ValueType::kBoolean:
      return {info.default_boolean};
    case ValueType::kInteger:
    case ValueType::kDuration:
    case ValueType::kByteSize:
      return {info.default_integer};
    case ValueType::kDouble:
      return {info.default_double};
    case ValueType::kEnum:
    case ValueType::kString:
      return {std::string(info.default_string)};
    case ValueType::kStringList:
      return {Elements(info.default_list)};
  }
  return {false};
}

std::optional<std::string> FindViolation(const SettingInfo& info,
                                         const Value& value) {
  if (!HoldsType(info.type, value)) {
    return absl::StrCat("expected ", TypeName(info.type));
  }
  if (IsIntegerLike(info.type)) {
    const auto number = std::get<std::int64_t>(value);
    if (number < info.min_integer || number > info.max_integer) {
      return "outside the allowed range";
    }
  } else if (info.type == ValueType::kDouble) {
    const auto number = std::get<double>(value);
    if (!std::isfinite(number) || number < info.min_double ||
        number > info.max_double) {
      return "outside the allowed range";
    }
  } else if (info.type == ValueType::kEnum ||
             (info.type == ValueType::kString && !info.allowed.empty())) {
    if (!Contains(info, std::get<std::string>(value))) {
      return "not an allowed value";
    }
  } else if (info.type == ValueType::kStringList && !info.allowed.empty()) {
    for (const auto& element : std::get<std::vector<std::string>>(value)) {
      if (!Contains(info, element)) {
        return absl::StrCat("element ", Quote(element), " is not allowed");
      }
    }
  }
  return std::nullopt;
}

std::string DescribeAllowed(const SettingInfo& info) {
  const std::string choices = absl::StrJoin(Elements(info.allowed), ", ");
  switch (info.type) {
    case ValueType::kBoolean:
      return "true or false";
    case ValueType::kInteger:
      return absl::StrCat("an integer from ", info.min_integer, " to ",
                          info.max_integer);
    case ValueType::kDuration:
      return absl::StrCat("whole milliseconds from ", info.min_integer, " to ",
                          info.max_integer);
    case ValueType::kByteSize:
      return absl::StrCat("a byte count from ", info.min_integer, " to ",
                          info.max_integer);
    case ValueType::kDouble:
      return absl::StrCat("a finite number from ",
                          FormatDouble(info.min_double, kShortPrecision),
                          " to ",
                          FormatDouble(info.max_double, kShortPrecision));
    case ValueType::kEnum:
      return absl::StrCat("one of: ", choices);
    case ValueType::kString:
      return info.allowed.empty() ? "any string"
                                  : absl::StrCat("one of: ", choices);
    case ValueType::kStringList:
      return info.allowed.empty()
                 ? "a comma-separated list of strings"
                 : absl::StrCat("a comma-separated list of: ", choices);
  }
  return "a valid value";
}

std::string FormatValue(const Value& value) {
  if (const auto* flag = std::get_if<bool>(&value)) {
    return *flag ? "true" : "false";
  }
  if (const auto* number = std::get_if<std::int64_t>(&value)) {
    return absl::StrCat(*number);
  }
  if (const auto* real = std::get_if<double>(&value)) {
    return FormatDouble(*real, kRoundTripPrecision);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return absl::StrJoin(std::get<std::vector<std::string>>(value), ",");
}

absl::Status RejectSetting(const base::UrlRedactor& redactor,
                           std::string_view key, std::string_view value,
                           std::string_view reason, std::string_view allowed) {
  const std::string shown =
      value.size() > kMaxShownValueBytes
          ? absl::StrCat(value.substr(0, kMaxShownValueBytes), "...")
          : std::string(value);
  const auto code = base::ErrorCode::kSettingsInvalid;
  const auto* entry = base::FindError(code);
  const absl::Status status = absl::InvalidArgumentError(absl::StrCat(
      "ALUMBRA-", static_cast<std::uint32_t>(code), ": ", entry->description,
      ": ", key, "=", shown, " ", reason, " (allowed: ", allowed, ")"));
  const std::map<std::string, std::string> context{
      {"setting", std::string(key)},
      {"setting_value", shown},
      {"reason", std::string(reason)},
      {"allowed", std::string(allowed)},
  };
  const base::ErrorDetail detail(code, entry->category, entry->severity,
                                 entry->retryable, context, nullptr);
  return base::AttachErrorDetail(status, detail, redactor);
}

}
