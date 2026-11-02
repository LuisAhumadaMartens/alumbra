#ifndef ALUMBRA_DESKTOP_UI_STRINGS_H_
#define ALUMBRA_DESKTOP_UI_STRINGS_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "alumbra/desktop_ui/text_shaper.h"
#include "umbra/ui/types.h"

namespace alumbra::desktop_ui {

enum class Locale { kEnglish, kPseudoAccented, kPseudoMirrored };

class StringCatalogue {
 public:
  explicit StringCatalogue(Locale locale = Locale::kEnglish)
      : locale_(locale) {}

  [[nodiscard]] std::string Resolve(
      std::string_view key, const std::vector<umbra::ui::TextArg>& args) const;
  [[nodiscard]] TextDirection Direction() const {
    return locale_ == Locale::kPseudoMirrored ? TextDirection::kRightToLeft
                                              : TextDirection::kLeftToRight;
  }
  [[nodiscard]] Locale GetLocale() const { return locale_; }

 private:
  Locale locale_;
};

[[nodiscard]] std::string FormatClock(std::int64_t microseconds,
                                      bool with_hours);
[[nodiscard]] std::string PseudoAccent(std::string_view text);

}

#endif
