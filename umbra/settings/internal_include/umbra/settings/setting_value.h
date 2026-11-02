#ifndef UMBRA_SETTINGS_SETTING_VALUE_H_
#define UMBRA_SETTINGS_SETTING_VALUE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

struct SettingLookup {
  std::size_t index;
  bool via_alias;
};

[[nodiscard]] std::optional<SettingLookup> FindSetting(const Registry& registry,
                                                       std::string_view name);

[[nodiscard]] Value DefaultValue(const SettingInfo& info);

[[nodiscard]] std::optional<std::string> FindViolation(const SettingInfo& info,
                                                       const Value& value);

[[nodiscard]] std::string DescribeAllowed(const SettingInfo& info);

[[nodiscard]] std::string FormatValue(const Value& value);

[[nodiscard]] absl::Status RejectSetting(const base::UrlRedactor& redactor,
                                         std::string_view key,
                                         std::string_view value,
                                         std::string_view reason,
                                         std::string_view allowed);

}

#endif
