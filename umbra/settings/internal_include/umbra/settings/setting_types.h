#ifndef UMBRA_SETTINGS_SETTING_TYPES_H_
#define UMBRA_SETTINGS_SETTING_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/time/time.h"
#include "umbra/settings/platform.h"

namespace umbra::settings {

enum class ValueType {
  kBoolean,
  kInteger,
  kDouble,
  kEnum,
  kString,
  kStringList,
  kDuration,
  kByteSize,
};

enum class ApplyMode { kLive, kNextLoad, kRestart };

enum class Audience { kIntegrator, kEndUser };

enum class Stability { kStable, kExperimental };

using Value = std::variant<bool, std::int64_t, double, std::string,
                           std::vector<std::string>>;

static_assert(kPlatformCount <= std::numeric_limits<std::uint16_t>::digits);

class PlatformSet {
 public:
  constexpr PlatformSet(std::initializer_list<Platform> platforms) {
    for (const Platform platform : platforms) {
      bits_ = static_cast<std::uint16_t>(bits_ | Bit(platform));
    }
  }

  [[nodiscard]] constexpr bool Contains(Platform platform) const {
    return (bits_ & Bit(platform)) != 0;
  }

 private:
  static constexpr std::uint16_t Bit(Platform platform) {
    return static_cast<std::uint16_t>(1U << static_cast<unsigned>(platform));
  }

  std::uint16_t bits_ = 0;
};

[[nodiscard]] constexpr std::size_t ListSize(std::string_view list) {
  std::size_t size = 0;
  for (const char character : list) {
    size += character == '\n' ? 1 : 0;
  }
  return size;
}

[[nodiscard]] constexpr std::string_view ListAt(std::string_view list,
                                                std::size_t index) {
  for (; index > 0; --index) {
    list.remove_prefix(list.find('\n') + 1);
  }
  return list.substr(0, list.find('\n'));
}

[[nodiscard]] constexpr std::optional<std::size_t> ListIndexOf(
    std::string_view list, std::string_view token) {
  std::size_t index = 0;
  while (!list.empty()) {
    const std::size_t end = list.find('\n');
    if (list.substr(0, end) == token) {
      return index;
    }
    list.remove_prefix(end + 1);
    ++index;
  }
  return std::nullopt;
}

struct SettingInfo {
  std::string_view name;
  ValueType type;
  ApplyMode apply;
  Audience audience;
  Stability stability;
  PlatformSet platforms;
  bool deprecated;
  std::string_view replacement;
  std::string_view auto_policy;
  std::int64_t min_integer;
  std::int64_t max_integer;
  double min_double;
  double max_double;
  std::string_view allowed;
  bool default_boolean;
  std::int64_t default_integer;
  double default_double;
  std::string_view default_string;
  std::string_view default_list;
};

struct AliasInfo {
  std::string_view name;
  std::size_t index;
};

struct Registry {
  std::span<const SettingInfo> settings;
  std::span<const AliasInfo> aliases;
};

template <typename T>
class Key {
 public:
  constexpr explicit Key(std::size_t index) : index_(index) {}

  [[nodiscard]] constexpr std::size_t Index() const { return index_; }

 private:
  std::size_t index_;
};

template <typename T>
struct ValueTraits;

template <>
struct ValueTraits<bool> {
  static constexpr ValueType kType = ValueType::kBoolean;
  static std::optional<Value> ToValue(bool value) { return Value(value); }
  static bool FromValue(const Value& value) { return std::get<bool>(value); }
};

template <>
struct ValueTraits<std::int64_t> {
  static constexpr ValueType kType = ValueType::kInteger;
  static std::optional<Value> ToValue(std::int64_t value) {
    return Value(value);
  }
  static std::int64_t FromValue(const Value& value) {
    return std::get<std::int64_t>(value);
  }
};

template <>
struct ValueTraits<double> {
  static constexpr ValueType kType = ValueType::kDouble;
  static std::optional<Value> ToValue(double value) { return Value(value); }
  static double FromValue(const Value& value) {
    return std::get<double>(value);
  }
};

template <>
struct ValueTraits<std::string> {
  static constexpr ValueType kType = ValueType::kString;
  static std::optional<Value> ToValue(std::string value) {
    return Value(std::move(value));
  }
  static std::string FromValue(const Value& value) {
    return std::get<std::string>(value);
  }
};

template <>
struct ValueTraits<std::vector<std::string>> {
  static constexpr ValueType kType = ValueType::kStringList;
  static std::optional<Value> ToValue(std::vector<std::string> value) {
    return Value(std::move(value));
  }
  static std::vector<std::string> FromValue(const Value& value) {
    return std::get<std::vector<std::string>>(value);
  }
};

template <>
struct ValueTraits<absl::Duration> {
  static constexpr ValueType kType = ValueType::kDuration;
  static std::optional<Value> ToValue(absl::Duration value) {
    const std::int64_t milliseconds = absl::ToInt64Milliseconds(value);
    if (absl::Milliseconds(milliseconds) != value) {
      return std::nullopt;
    }
    return Value(milliseconds);
  }
  static absl::Duration FromValue(const Value& value) {
    return absl::Milliseconds(std::get<std::int64_t>(value));
  }
};

template <>
struct ValueTraits<std::uint64_t> {
  static constexpr ValueType kType = ValueType::kByteSize;
  static std::optional<Value> ToValue(std::uint64_t value) {
    if (value >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return Value(static_cast<std::int64_t>(value));
  }
  static std::uint64_t FromValue(const Value& value) {
    return static_cast<std::uint64_t>(std::get<std::int64_t>(value));
  }
};

template <typename E>
struct EnumNames;

template <typename E>
  requires std::is_enum_v<E>
struct ValueTraits<E> {
  static constexpr ValueType kType = ValueType::kEnum;
  static std::optional<Value> ToValue(E value) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= ListSize(EnumNames<E>::kNames)) {
      return std::nullopt;
    }
    return Value(std::string(ListAt(EnumNames<E>::kNames, index)));
  }
  static E FromValue(const Value& value) {
    return static_cast<E>(
        ListIndexOf(EnumNames<E>::kNames, std::get<std::string>(value))
            .value());
  }
};

}

#endif
