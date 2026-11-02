#include "umbra/player/language_preferences.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace umbra::player::internal {
namespace {

char AsciiLower(char value) {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }
  return value;
}

bool EqualsCaseInsensitive(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (AsciiLower(left[index]) != AsciiLower(right[index])) {
      return false;
    }
  }
  return true;
}

std::string TruncateLanguageRange(std::string range) {
  const std::size_t separator = range.rfind('-');
  if (separator == std::string::npos) {
    return {};
  }
  range.erase(separator);
  const std::size_t previous_separator = range.rfind('-');
  const std::size_t last_subtag_start =
      previous_separator == std::string::npos ? 0 : previous_separator + 1;
  if (range.size() - last_subtag_start == 1 &&
      previous_separator != std::string::npos) {
    range.erase(previous_separator);
  }
  return range;
}

}

bool MatchesLanguageRange(std::string_view range, std::string_view language) {
  if (range.empty() || language.empty()) {
    return false;
  }
  if (range == "*") {
    return true;
  }
  if (language.size() < range.size() ||
      !EqualsCaseInsensitive(range, language.substr(0, range.size()))) {
    return false;
  }
  return language.size() == range.size() || language[range.size()] == '-';
}

std::optional<std::size_t> FindPreferredLanguage(
    std::span<const std::string> preferences,
    std::span<const std::string> languages) {
  for (const std::string& preference : preferences) {
    std::string range = preference;
    while (!range.empty()) {
      for (std::size_t index = 0; index < languages.size(); ++index) {
        if (MatchesLanguageRange(range, languages[index])) {
          return index;
        }
      }
      range = TruncateLanguageRange(range);
    }
  }
  return std::nullopt;
}

}
