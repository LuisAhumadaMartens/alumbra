#include <array>
#include <cstddef>
#include <string_view>

#include "umbra/settings/platform.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/testing/all_types_keys.h"

namespace umbra::settings::all_types {
namespace {

constexpr std::string_view kAllowed4 =
    "auto\n"
    "manual\n";

constexpr std::string_view kDefault6 = "first\n";

constexpr std::array<SettingInfo, 8> kSettings{{
    SettingInfo{
        .name = "test.capacity_bytes",
        .type = ValueType::kByteSize,
        .apply = ApplyMode::kRestart,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = true,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 4096,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 1024,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.count",
        .type = ValueType::kInteger,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = -5,
        .max_integer = 5,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 2,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.enabled",
        .type = ValueType::kBoolean,
        .apply = ApplyMode::kLive,
        .audience = Audience::kEndUser,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = true,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.label",
        .type = ValueType::kString,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.mode",
        .type = ValueType::kEnum,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = kAllowed4,
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "auto",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.ratio_percent",
        .type = ValueType::kDouble,
        .apply = ApplyMode::kRestart,
        .audience = Audience::kEndUser,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 100.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 50.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "test.tags",
        .type = ValueType::kStringList,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "",
        .default_list = kDefault6,
    },
    SettingInfo{
        .name = "test.timeout_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 1000,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 100,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
}};

constexpr std::array<AliasInfo, 1> kAliases{{
    AliasInfo{
        .name = "test.old_timeout_ms",
        .index = 7,
    },
}};

}

Registry GetRegistry() {
  return Registry{.settings = kSettings, .aliases = kAliases};
}

}
