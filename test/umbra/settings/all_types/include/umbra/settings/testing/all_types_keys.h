#ifndef UMBRA_SETTINGS_TESTING_ALL_TYPES_KEYS_H_
#define UMBRA_SETTINGS_TESTING_ALL_TYPES_KEYS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings::all_types {

enum class TestMode {
  kAuto,
  kManual,
};

inline constexpr Key<std::uint64_t> kTestCapacityBytes{0};
inline constexpr Key<std::int64_t> kTestCount{1};
inline constexpr Key<bool> kTestEnabled{2};
inline constexpr Key<std::string> kTestLabel{3};
inline constexpr Key<TestMode> kTestMode{4};
inline constexpr Key<double> kTestRatioPercent{5};
inline constexpr Key<std::vector<std::string>> kTestTags{6};
inline constexpr Key<absl::Duration> kTestTimeoutMs{7};

[[nodiscard]] Registry GetRegistry();

}

namespace umbra::settings {

template <>
struct EnumNames<all_types::TestMode> {
  static constexpr std::string_view kNames =
      "auto\n"
      "manual\n";
};

}

#endif
