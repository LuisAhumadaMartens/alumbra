#ifndef UMBRA_PLAYER_LANGUAGE_PREFERENCES_H_
#define UMBRA_PLAYER_LANGUAGE_PREFERENCES_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace umbra::player::internal {

[[nodiscard]] bool MatchesLanguageRange(std::string_view range,
                                        std::string_view language);

[[nodiscard]] std::optional<std::size_t> FindPreferredLanguage(
    std::span<const std::string> preferences,
    std::span<const std::string> languages);

}

#endif
