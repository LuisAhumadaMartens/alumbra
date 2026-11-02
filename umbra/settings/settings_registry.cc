#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

#include "umbra/settings/platform.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/settings_keys.h"

namespace umbra::settings {
namespace {

constexpr std::string_view kAllowed8 =
    "net\n"
    "manifest\n"
    "abr\n"
    "buffer\n"
    "demux\n"
    "decode\n"
    "render\n"
    "audio\n"
    "text\n"
    "ads\n"
    "drm\n"
    "settings\n"
    "player\n"
    "port\n";

constexpr std::string_view kAllowed9 =
    "verbose\n"
    "debug\n"
    "info\n"
    "warning\n"
    "error\n";

constexpr std::string_view kAllowed10 =
    "net\n"
    "manifest\n"
    "abr\n"
    "buffer\n"
    "demux\n"
    "decode\n"
    "render\n"
    "audio\n"
    "text\n"
    "ads\n"
    "drm\n"
    "settings\n"
    "player\n"
    "port\n";

constexpr std::string_view kAllowed13 =
    "auto\n"
    "hls\n"
    "dash\n"
    "mp4\n"
    "matroska\n"
    "mpegts\n"
    "flv\n";

constexpr std::array<SettingInfo, 14> kSettings{{
    SettingInfo{
        .name = "abr.rendition_exclusion_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = std::numeric_limits<std::int64_t>::max(),
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 60000,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "engine.threads.decoder_max",
        .type = ValueType::kInteger,
        .apply = ApplyMode::kRestart,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "hardware_concurrency_budget",
        .min_integer = 0,
        .max_integer = 2147483647,
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
        .name = "engine.threads.max_workers",
        .type = ValueType::kInteger,
        .apply = ApplyMode::kRestart,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "hardware_concurrency_budget",
        .min_integer = 0,
        .max_integer = 2147483647,
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
        .name = "engine.threads.yield_budget_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "half_frame_interval_capped",
        .min_integer = 0,
        .max_integer = 8,
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
        .name = "network.pathway_exclusion_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = std::numeric_limits<std::int64_t>::max(),
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 300000,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "network.retry.initial_backoff_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = std::numeric_limits<std::int64_t>::max(),
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 500,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "network.retry.max_attempts",
        .type = ValueType::kInteger,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 2147483647,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 3,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "network.retry.max_backoff_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = std::numeric_limits<std::int64_t>::max(),
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 8000,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "observability.log.categories",
        .type = ValueType::kStringList,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = kAllowed8,
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "observability.log.level",
        .type = ValueType::kEnum,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = kAllowed9,
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "info",
        .default_list = "",
    },
    SettingInfo{
        .name = "observability.trace.categories",
        .type = ValueType::kStringList,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kStable,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = kAllowed10,
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "player.events.timeupdate_interval_ms",
        .type = ValueType::kDuration,
        .apply = ApplyMode::kLive,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 1,
        .max_integer = std::numeric_limits<std::int64_t>::max(),
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 250,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "source.read_ahead_bytes",
        .type = ValueType::kByteSize,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 65536,
        .max_integer = 67108864,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = "",
        .default_boolean = false,
        .default_integer = 2097152,
        .default_double = 0.0,
        .default_string = "",
        .default_list = "",
    },
    SettingInfo{
        .name = "source.type_hint",
        .type = ValueType::kEnum,
        .apply = ApplyMode::kNextLoad,
        .audience = Audience::kIntegrator,
        .stability = Stability::kExperimental,
        .platforms =
            PlatformSet{
                Platform::kLinux,
                Platform::kMacos,
                Platform::kWindows,
                Platform::kAndroid,
                Platform::kGoogleTv,
                Platform::kIos,
                Platform::kIpados,
                Platform::kTvos,
                Platform::kWeb,
            },
        .deprecated = false,
        .replacement = "",
        .auto_policy = "",
        .min_integer = 0,
        .max_integer = 0,
        .min_double = 0.0,
        .max_double = 0.0,
        .allowed = kAllowed13,
        .default_boolean = false,
        .default_integer = 0,
        .default_double = 0.0,
        .default_string = "auto",
        .default_list = "",
    },
}};

constexpr std::array<AliasInfo, 0> kAliases{};

}

Registry GetRegistry() {
  return Registry{.settings = kSettings, .aliases = kAliases};
}

}
