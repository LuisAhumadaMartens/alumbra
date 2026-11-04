#include "umbra/player/language_preferences.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::player::internal {
namespace {

struct LanguageMatchCase {
  std::string range;
  std::string language;
  bool matches;
};

TEST(LanguagePreferencesTest, MatchesBasicLanguageRanges) {
  const std::vector<LanguageMatchCase> cases = {
      {"en-GB", "en-GB", true},
      {"es", "es-MX", true},
      {"zh-Hant", "zh-Hant-TW", true},
      {"zh-Hant", "zh-Hans-CN", false},
      {"und", "und", true},
      {"und", "", false},
      {"en", "", false},
      {"en", "english", false},
      {"EN", "en-us", true},
      {"*", "und", true},
      {"*", "", false},
  };

  for (const LanguageMatchCase& test_case : cases) {
    EXPECT_EQ(MatchesLanguageRange(test_case.range, test_case.language),
              test_case.matches)
        << test_case.range << " / " << test_case.language;
  }
}

TEST(LanguagePreferencesTest, UsesPreferencePriorityBeforeTrackOrder) {
  const std::vector<std::string> preferences = {"fr", "es"};
  const std::vector<std::string> languages = {"es-MX", "fr-CA"};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), 1U);
}

TEST(LanguagePreferencesTest, UsesFirstTrackForOneMatchingRange) {
  const std::vector<std::string> preferences = {"es"};
  const std::vector<std::string> languages = {"es-MX", "es-ES"};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), 0U);
}

TEST(LanguagePreferencesTest, TruncatesUnmatchedRanges) {
  const std::vector<std::string> preferences = {"zh-Hant-TW"};
  const std::vector<std::string> languages = {"zh-Hant-HK", "zh-Hans-CN"};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), 0U);
}

TEST(LanguagePreferencesTest, TruncationRemovesSingletonExtensions) {
  const std::vector<std::string> preferences = {"de-DE-x-goethe"};
  const std::vector<std::string> languages = {"de-DE"};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), 0U);
}

TEST(LanguagePreferencesTest, MissingLanguagesDoNotMatch) {
  const std::vector<std::string> preferences = {"es", "und"};
  const std::vector<std::string> languages = {"", ""};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), std::nullopt);
}

TEST(LanguagePreferencesTest, EmptyPreferencesDoNotSelectALanguage) {
  const std::vector<std::string> preferences;
  const std::vector<std::string> languages = {"en", "es"};

  EXPECT_EQ(FindPreferredLanguage(preferences, languages), std::nullopt);
}

TEST(LanguagePreferencesTest, DescriptionPreferenceStaysWithinBestLanguage) {
  const std::vector<std::string> preferences = {"es"};
  const std::vector<std::string> languages = {"es-MX", "es-MX", "en-US"};
  const std::vector<std::uint8_t> descriptions = {0, 1, 1};

  EXPECT_EQ(FindPreferredAudioTrack(preferences, languages, descriptions, true),
            1U);
}

TEST(LanguagePreferencesTest, KeepsMainTrackWhenLanguageHasNoDescription) {
  const std::vector<std::string> preferences = {"es"};
  const std::vector<std::string> languages = {"es-MX", "en-US"};
  const std::vector<std::uint8_t> descriptions = {0, 1};

  EXPECT_EQ(FindPreferredAudioTrack(preferences, languages, descriptions, true),
            0U);
}

}
}
