#include "umbra/observability/category.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace umbra::observability {
namespace {

constexpr std::array<std::string_view, kCategoryCount> kCategoryNames = {
    "net",   "manifest", "abr", "buffer", "demux",    "decode", "render",
    "audio", "text",     "ads", "drm",    "settings", "player", "port"};

constexpr std::array<std::string_view, 5> kLevelNames = {
    "verbose", "debug", "info", "warning", "error"};

}

std::string_view CategoryName(Category category) {
  const auto index = static_cast<std::size_t>(category);
  return index < kCategoryNames.size() ? kCategoryNames.at(index)
                                       : std::string_view("unknown");
}

std::string_view LevelName(Level level) {
  const auto index = static_cast<std::size_t>(level);
  return index < kLevelNames.size() ? kLevelNames.at(index)
                                    : std::string_view("unknown");
}

std::optional<Category> ParseCategory(std::string_view name) {
  for (std::size_t i = 0; i < kCategoryNames.size(); ++i) {
    if (kCategoryNames.at(i) == name) {
      return static_cast<Category>(i);
    }
  }
  return std::nullopt;
}

}
