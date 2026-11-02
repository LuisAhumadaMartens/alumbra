#include "umbra/settings/setting_parser.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/setting_value.h"

namespace umbra::settings {
namespace {

bool HasEdgeSpace(std::string_view text) {
  return !text.empty() &&
         (std::isspace(static_cast<unsigned char>(text.front())) != 0 ||
          std::isspace(static_cast<unsigned char>(text.back())) != 0);
}

bool IsDecimal(std::string_view text) {
  if (text.starts_with('-')) {
    text.remove_prefix(1);
  }
  return !text.empty() && std::ranges::all_of(text, [](char character) {
    return character >= '0' && character <= '9';
  });
}

std::vector<std::string> Split(std::string_view text) {
  std::vector<std::string> parts;
  if (text.empty()) {
    return parts;
  }
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = text.find(',', start);
    if (comma == std::string_view::npos) {
      parts.emplace_back(text.substr(start));
      return parts;
    }
    parts.emplace_back(text.substr(start, comma - start));
    start = comma + 1;
  }
}

}

absl::StatusOr<Assignment> ParseAssignment(std::string_view text,
                                           const base::UrlRedactor& redactor) {
  const std::size_t equals = text.find('=');
  if (equals == std::string_view::npos || equals == 0) {
    return RejectSetting(redactor, text.substr(0, equals), "",
                         "is not a key=value assignment", "key=value");
  }
  return Assignment{.key = text.substr(0, equals),
                    .value = text.substr(equals + 1)};
}

absl::StatusOr<Value> ParseValue(const SettingInfo& info, std::string_view text,
                                 const base::UrlRedactor& redactor) {
  const auto reject = [&](std::string_view reason) {
    return RejectSetting(redactor, info.name, text, reason,
                         DescribeAllowed(info));
  };
  Value value;
  switch (info.type) {
    case ValueType::kBoolean:
      if (text != "true" && text != "false") {
        return reject("is not a boolean");
      }
      value = text == "true";
      break;
    case ValueType::kInteger:
    case ValueType::kDuration:
    case ValueType::kByteSize: {
      std::int64_t number = 0;
      if (!IsDecimal(text)) {
        return reject("is not an integer");
      }
      if (!absl::SimpleAtoi(text, &number)) {
        return reject("is out of the representable range");
      }
      value = number;
      break;
    }
    case ValueType::kDouble: {
      double number = 0.0;
      if (HasEdgeSpace(text) || !absl::SimpleAtod(text, &number)) {
        return reject("is not a number");
      }
      value = number;
      break;
    }
    case ValueType::kEnum:
    case ValueType::kString:
      value = std::string(text);
      break;
    case ValueType::kStringList: {
      std::vector<std::string> elements = Split(text);
      for (const auto& element : elements) {
        if (element.empty()) {
          return reject("contains an empty list element");
        }
      }
      value = std::move(elements);
      break;
    }
  }
  if (const auto violation = FindViolation(info, value)) {
    return reject(*violation);
  }
  return value;
}

}
