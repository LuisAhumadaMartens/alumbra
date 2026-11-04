#ifndef UMBRA_TEXT_STYLE_H_
#define UMBRA_TEXT_STYLE_H_

#include <optional>

#include "umbra/settings/store.h"

namespace umbra::text {

template <typename T>
struct StyleField {
  T value;
  bool overridden = false;
};

template <typename T>
[[nodiscard]] StyleField<T> ResolveStyleField(
    const T& author_value, const std::optional<T>& platform_value,
    const settings::Snapshot& user_preferences, settings::Key<T> key) {
  const T platform_or_author = platform_value.value_or(author_value);
  return {
      .value = user_preferences.ResolveAuthorValue(key, platform_or_author),
      .overridden = user_preferences.IsExplicitlySet(key) ||
                    platform_value.has_value(),
  };
}

}

#endif
