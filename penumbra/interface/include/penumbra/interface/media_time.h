#ifndef PENUMBRA_INTERFACE_MEDIA_TIME_H_
#define PENUMBRA_INTERFACE_MEDIA_TIME_H_

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "penumbra/interface/rational.h"

namespace penumbra {

namespace internal {

inline constexpr int64_t kUnknownMicros = std::numeric_limits<int64_t>::min();
inline constexpr int64_t kNegativeInfiniteMicros = kUnknownMicros + 1;
inline constexpr int64_t kInfiniteMicros = std::numeric_limits<int64_t>::max();
inline constexpr int64_t kMaxFiniteMicros = kInfiniteMicros - 1;
inline constexpr int64_t kMinFiniteMicros = kNegativeInfiniteMicros + 1;

constexpr bool IsInfiniteMicros(int64_t micros) {
  return micros == kInfiniteMicros || micros == kNegativeInfiniteMicros;
}

constexpr int64_t ClampMicros(int64_t micros) {
  if (micros > kMaxFiniteMicros) {
    return kInfiniteMicros;
  }
  if (micros < kMinFiniteMicros) {
    return kNegativeInfiniteMicros;
  }
  return micros;
}

constexpr int64_t ScaleMicros(int64_t value, int64_t factor) {
  const int64_t bound = kMaxFiniteMicros / factor;
  if (value > bound) {
    return kInfiniteMicros;
  }
  if (value < -bound) {
    return kNegativeInfiniteMicros;
  }
  return value * factor;
}

constexpr int64_t AddMicros(int64_t a, int64_t b) {
  if (a == kUnknownMicros || b == kUnknownMicros) {
    return kUnknownMicros;
  }
  if (IsInfiniteMicros(a) && IsInfiniteMicros(b)) {
    return a == b ? a : kUnknownMicros;
  }
  if (IsInfiniteMicros(a)) {
    return a;
  }
  if (IsInfiniteMicros(b)) {
    return b;
  }
  if (b > 0 && a > kMaxFiniteMicros - b) {
    return kInfiniteMicros;
  }
  if (b < 0 && a < kMinFiniteMicros - b) {
    return kNegativeInfiniteMicros;
  }
  return a + b;
}

constexpr int64_t NegateMicros(int64_t micros) {
  if (micros == kUnknownMicros) {
    return kUnknownMicros;
  }
  if (micros == kInfiniteMicros) {
    return kNegativeInfiniteMicros;
  }
  if (micros == kNegativeInfiniteMicros) {
    return kInfiniteMicros;
  }
  return -micros;
}

inline std::string MicrosToString(int64_t micros) {
  if (micros == kUnknownMicros) {
    return "unknown";
  }
  if (micros == kInfiniteMicros) {
    return "+inf";
  }
  if (micros == kNegativeInfiniteMicros) {
    return "-inf";
  }
  return std::to_string(micros) + "us";
}

}

class TimeDelta {
 public:
  constexpr TimeDelta() = default;

  static constexpr TimeDelta FromMicroseconds(int64_t microseconds) {
    return TimeDelta(internal::ClampMicros(microseconds));
  }
  static constexpr TimeDelta FromMilliseconds(int64_t milliseconds) {
    return TimeDelta(internal::ScaleMicros(milliseconds, 1'000));
  }
  static constexpr TimeDelta FromSeconds(int64_t seconds) {
    return TimeDelta(internal::ScaleMicros(seconds, 1'000'000));
  }
  static constexpr TimeDelta Zero() { return {}; }
  static constexpr TimeDelta Unknown() {
    return TimeDelta(internal::kUnknownMicros);
  }
  static constexpr TimeDelta Infinite() {
    return TimeDelta(internal::kInfiniteMicros);
  }
  static constexpr TimeDelta NegativeInfinite() {
    return TimeDelta(internal::kNegativeInfiniteMicros);
  }

  [[nodiscard]] constexpr bool is_unknown() const {
    return micros_ == internal::kUnknownMicros;
  }
  [[nodiscard]] constexpr bool is_infinite() const {
    return internal::IsInfiniteMicros(micros_);
  }
  [[nodiscard]] constexpr bool is_finite() const {
    return !is_unknown() && !is_infinite();
  }
  [[nodiscard]] constexpr int64_t InMicroseconds() const { return micros_; }

  friend constexpr auto operator<=>(TimeDelta, TimeDelta) = default;

  friend constexpr TimeDelta operator+(TimeDelta a, TimeDelta b) {
    return TimeDelta(internal::AddMicros(a.micros_, b.micros_));
  }
  friend constexpr TimeDelta operator-(TimeDelta a, TimeDelta b) {
    return TimeDelta(
        internal::AddMicros(a.micros_, internal::NegateMicros(b.micros_)));
  }
  friend constexpr TimeDelta operator-(TimeDelta value) {
    return TimeDelta(internal::NegateMicros(value.micros_));
  }

  [[nodiscard]] std::string ToString() const {
    return internal::MicrosToString(micros_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, TimeDelta value) {
    sink.Append(value.ToString());
  }

 private:
  explicit constexpr TimeDelta(int64_t micros) : micros_(micros) {}

  int64_t micros_ = 0;
};

class MediaTime {
 public:
  constexpr MediaTime() = default;

  static constexpr MediaTime FromMicroseconds(int64_t microseconds) {
    return MediaTime(internal::ClampMicros(microseconds));
  }
  static constexpr MediaTime FromMilliseconds(int64_t milliseconds) {
    return MediaTime(internal::ScaleMicros(milliseconds, 1'000));
  }
  static constexpr MediaTime FromSeconds(int64_t seconds) {
    return MediaTime(internal::ScaleMicros(seconds, 1'000'000));
  }
  static constexpr MediaTime Zero() { return {}; }
  static constexpr MediaTime Unknown() {
    return MediaTime(internal::kUnknownMicros);
  }
  static constexpr MediaTime Infinite() {
    return MediaTime(internal::kInfiniteMicros);
  }
  static constexpr MediaTime NegativeInfinite() {
    return MediaTime(internal::kNegativeInfiniteMicros);
  }

  [[nodiscard]] constexpr bool is_unknown() const {
    return micros_ == internal::kUnknownMicros;
  }
  [[nodiscard]] constexpr bool is_infinite() const {
    return internal::IsInfiniteMicros(micros_);
  }
  [[nodiscard]] constexpr bool is_finite() const {
    return !is_unknown() && !is_infinite();
  }
  [[nodiscard]] constexpr int64_t InMicroseconds() const { return micros_; }

  friend constexpr auto operator<=>(MediaTime, MediaTime) = default;

  friend constexpr MediaTime operator+(MediaTime time, TimeDelta delta) {
    return MediaTime(internal::AddMicros(time.micros_, delta.InMicroseconds()));
  }
  friend constexpr MediaTime operator-(MediaTime time, TimeDelta delta) {
    return MediaTime(internal::AddMicros(
        time.micros_, internal::NegateMicros(delta.InMicroseconds())));
  }
  friend constexpr TimeDelta operator-(MediaTime a, MediaTime b) {
    return DeltaFromMicros(
        internal::AddMicros(a.micros_, internal::NegateMicros(b.micros_)));
  }

  [[nodiscard]] std::string ToString() const {
    return "@" + internal::MicrosToString(micros_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, MediaTime value) {
    sink.Append(value.ToString());
  }

 private:
  explicit constexpr MediaTime(int64_t micros) : micros_(micros) {}

  static constexpr TimeDelta DeltaFromMicros(int64_t micros) {
    if (micros == internal::kUnknownMicros) {
      return TimeDelta::Unknown();
    }
    return TimeDelta::FromMicroseconds(micros);
  }

  int64_t micros_ = 0;
};

struct Timestamp {
  static constexpr int64_t kUnknown = std::numeric_limits<int64_t>::min();

  int64_t value = kUnknown;
  Rational time_base;

  [[nodiscard]] constexpr bool is_known() const { return value != kUnknown; }

  friend constexpr bool operator==(const Timestamp&,
                                   const Timestamp&) = default;

  [[nodiscard]] std::string ToString() const {
    if (!is_known()) {
      return "unknown@" + time_base.ToString();
    }
    return std::to_string(value) + "@" + time_base.ToString();
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Timestamp& value) {
    sink.Append(value.ToString());
  }
};

inline constexpr Rational kMicrosecondTimeBase = Rational::Of(1, 1'000'000);

namespace internal {

constexpr std::optional<int64_t> TimestampToMicros(Timestamp timestamp,
                                                   Rounding rounding) {
  if (!timestamp.is_known()) {
    return kUnknownMicros;
  }
  const std::optional<int64_t> micros = Rescale(
      timestamp.value, timestamp.time_base, kMicrosecondTimeBase, rounding);
  if (!micros.has_value() || *micros < kMinFiniteMicros ||
      *micros > kMaxFiniteMicros) {
    return std::nullopt;
  }
  return micros;
}

constexpr std::optional<Timestamp> MicrosToTimestamp(int64_t micros,
                                                     Rational time_base,
                                                     Rounding rounding) {
  if (!time_base.is_positive() || IsInfiniteMicros(micros)) {
    return std::nullopt;
  }
  if (micros == kUnknownMicros) {
    return Timestamp{.value = Timestamp::kUnknown, .time_base = time_base};
  }
  const std::optional<int64_t> value =
      Rescale(micros, kMicrosecondTimeBase, time_base, rounding);
  if (!value.has_value() || *value == Timestamp::kUnknown) {
    return std::nullopt;
  }
  return Timestamp{.value = *value, .time_base = time_base};
}

}

constexpr std::optional<MediaTime> ToMediaTime(Timestamp timestamp,
                                               Rounding rounding) {
  const std::optional<int64_t> micros =
      internal::TimestampToMicros(timestamp, rounding);
  if (!micros.has_value()) {
    return std::nullopt;
  }
  return *micros == internal::kUnknownMicros
             ? MediaTime::Unknown()
             : MediaTime::FromMicroseconds(*micros);
}

constexpr std::optional<TimeDelta> ToTimeDelta(Timestamp duration,
                                               Rounding rounding) {
  const std::optional<int64_t> micros =
      internal::TimestampToMicros(duration, rounding);
  if (!micros.has_value()) {
    return std::nullopt;
  }
  return *micros == internal::kUnknownMicros
             ? TimeDelta::Unknown()
             : TimeDelta::FromMicroseconds(*micros);
}

constexpr std::optional<Timestamp> ToTimestamp(MediaTime time,
                                               Rational time_base,
                                               Rounding rounding) {
  return internal::MicrosToTimestamp(time.InMicroseconds(), time_base,
                                     rounding);
}

constexpr std::optional<Timestamp> ToTimestamp(TimeDelta duration,
                                               Rational time_base,
                                               Rounding rounding) {
  return internal::MicrosToTimestamp(duration.InMicroseconds(), time_base,
                                     rounding);
}

constexpr std::optional<Timestamp> Rescale(Timestamp timestamp,
                                           Rational time_base,
                                           Rounding rounding) {
  if (!time_base.is_positive()) {
    return std::nullopt;
  }
  if (!timestamp.is_known()) {
    return Timestamp{.value = Timestamp::kUnknown, .time_base = time_base};
  }
  const std::optional<int64_t> value =
      Rescale(timestamp.value, timestamp.time_base, time_base, rounding);
  if (!value.has_value() || *value == Timestamp::kUnknown) {
    return std::nullopt;
  }
  return Timestamp{.value = *value, .time_base = time_base};
}

}

#endif
