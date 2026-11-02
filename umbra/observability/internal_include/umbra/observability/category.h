#ifndef UMBRA_OBSERVABILITY_CATEGORY_H_
#define UMBRA_OBSERVABILITY_CATEGORY_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace umbra::observability {

enum class Category : std::uint8_t {
  kNet,
  kManifest,
  kAbr,
  kBuffer,
  kDemux,
  kDecode,
  kRender,
  kAudio,
  kText,
  kAds,
  kDrm,
  kSettings,
  kPlayer,
  kPort,
};

enum class Level : std::uint8_t {
  kVerbose,
  kDebug,
  kInfo,
  kWarning,
  kError,
};

inline constexpr std::size_t kCategoryCount = 14;
inline constexpr int kLogOff = 5;
inline constexpr std::uint32_t kAllCategories = (1U << kCategoryCount) - 1;

[[nodiscard]] constexpr std::uint32_t CategoryBit(Category category) {
  return 1U << static_cast<unsigned>(category);
}

[[nodiscard]] std::string_view CategoryName(Category category);
[[nodiscard]] std::string_view LevelName(Level level);
[[nodiscard]] std::optional<Category> ParseCategory(std::string_view name);

}

#endif
