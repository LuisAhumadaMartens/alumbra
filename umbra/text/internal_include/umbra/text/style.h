#ifndef UMBRA_TEXT_STYLE_H_
#define UMBRA_TEXT_STYLE_H_

#include <optional>

namespace umbra::text {

template <typename T>
struct StyleField {
  T value;
  bool overridden = false;
};

template <typename T>
[[nodiscard]] StyleField<T> ResolveStyleField(
    const T& author_value, const std::optional<T>& platform_value,
    const T& user_value, bool user_explicit) {
  if (user_explicit) {
    return {.value = user_value, .overridden = true};
  }
  if (platform_value.has_value()) {
    return {.value = *platform_value, .overridden = true};
  }
  return {.value = author_value, .overridden = false};
}

}

#endif
