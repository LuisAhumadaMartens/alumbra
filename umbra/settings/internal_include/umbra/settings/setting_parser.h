#ifndef UMBRA_SETTINGS_SETTING_PARSER_H_
#define UMBRA_SETTINGS_SETTING_PARSER_H_

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

struct Assignment {
  std::string_view key;
  std::string_view value;
};

[[nodiscard]] absl::StatusOr<Assignment> ParseAssignment(
    std::string_view text, const base::UrlRedactor& redactor);

[[nodiscard]] absl::StatusOr<Value> ParseValue(
    const SettingInfo& info, std::string_view text,
    const base::UrlRedactor& redactor);

}

#endif
