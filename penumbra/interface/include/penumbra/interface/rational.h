#ifndef PENUMBRA_INTERFACE_RATIONAL_H_
#define PENUMBRA_INTERFACE_RATIONAL_H_

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <string>

namespace penumbra {

enum class Rounding : uint8_t {
  kTowardZero,
  kAwayFromZero,
  kDown,
  kUp,
  kNearest,
};

class Rational {
 public:
  constexpr Rational() = default;

  static constexpr std::optional<Rational> Create(int64_t numerator,
                                                  int64_t denominator) {
    constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
    if (denominator == 0 || numerator == kMin || denominator == kMin) {
      return std::nullopt;
    }
    if (denominator < 0) {
      numerator = -numerator;
      denominator = -denominator;
    }
    const int64_t divisor = std::gcd(numerator, denominator);
    return Rational(numerator / divisor, denominator / divisor);
  }

  static consteval Rational Of(int64_t numerator, int64_t denominator) {
    const auto result = Create(numerator, denominator);
    if (!result.has_value()) {
      std::abort();
    }
    return *result;
  }

  [[nodiscard]] constexpr int64_t numerator() const { return numerator_; }
  [[nodiscard]] constexpr int64_t denominator() const { return denominator_; }
  [[nodiscard]] constexpr bool is_positive() const { return numerator_ > 0; }

  friend constexpr bool operator==(Rational, Rational) = default;

  [[nodiscard]] std::string ToString() const {
    return std::to_string(numerator_) + "/" + std::to_string(denominator_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, Rational value) {
    sink.Append(value.ToString());
  }

 private:
  constexpr Rational(int64_t numerator, int64_t denominator)
      : numerator_(numerator), denominator_(denominator) {}

  int64_t numerator_ = 0;
  int64_t denominator_ = 1;
};

namespace internal {

struct WideProduct {
  uint64_t high = 0;
  uint64_t low = 0;
};

struct WideQuotient {
  uint64_t quotient = 0;
  uint64_t remainder = 0;
};

constexpr WideProduct MultiplyWide(uint64_t a, uint64_t b) {
  constexpr uint64_t kLowMask = 0xFFFFFFFFULL;
  const uint64_t a_low = a & kLowMask;
  const uint64_t a_high = a >> 32;
  const uint64_t b_low = b & kLowMask;
  const uint64_t b_high = b >> 32;
  const uint64_t low_low = a_low * b_low;
  const uint64_t low_high = a_low * b_high;
  const uint64_t high_low = a_high * b_low;
  const uint64_t high_high = a_high * b_high;
  const uint64_t middle =
      (low_low >> 32) + (low_high & kLowMask) + (high_low & kLowMask);
  return {
      .high = high_high + (low_high >> 32) + (high_low >> 32) + (middle >> 32),
      .low = (middle << 32) | (low_low & kLowMask)};
}

constexpr std::optional<WideQuotient> DivideWide(WideProduct dividend,
                                                 uint64_t divisor) {
  constexpr auto kMaxDivisor =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  if (divisor == 0 || divisor > kMaxDivisor || dividend.high >= divisor) {
    return std::nullopt;
  }
  uint64_t remainder = dividend.high;
  uint64_t quotient = 0;
  for (int bit = 63; bit >= 0; --bit) {
    remainder = (remainder << 1) | ((dividend.low >> bit) & 1U);
    quotient <<= 1;
    if (remainder >= divisor) {
      remainder -= divisor;
      quotient |= 1U;
    }
  }
  return WideQuotient{.quotient = quotient, .remainder = remainder};
}

constexpr bool RoundsMagnitudeUp(Rounding rounding, bool negative,
                                 uint64_t remainder, uint64_t divisor) {
  if (remainder == 0) {
    return false;
  }
  switch (rounding) {
    case Rounding::kTowardZero:
      return false;
    case Rounding::kAwayFromZero:
      return true;
    case Rounding::kDown:
      return negative;
    case Rounding::kUp:
      return !negative;
    case Rounding::kNearest:
      return remainder >= divisor - remainder;
  }
  return false;
}

}

constexpr std::optional<int64_t> Rescale(int64_t value, Rational from,
                                         Rational to, Rounding rounding) {
  if (!from.is_positive() || !to.is_positive()) {
    return std::nullopt;
  }
  constexpr auto kMax =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  const int64_t numerator_gcd = std::gcd(from.numerator(), to.numerator());
  const int64_t denominator_gcd =
      std::gcd(from.denominator(), to.denominator());
  const internal::WideProduct scale = internal::MultiplyWide(
      static_cast<uint64_t>(from.numerator() / numerator_gcd),
      static_cast<uint64_t>(to.denominator() / denominator_gcd));
  const internal::WideProduct divisor = internal::MultiplyWide(
      static_cast<uint64_t>(from.denominator() / denominator_gcd),
      static_cast<uint64_t>(to.numerator() / numerator_gcd));
  if (scale.high != 0 || scale.low > kMax || divisor.high != 0 ||
      divisor.low > kMax) {
    return std::nullopt;
  }
  const bool negative = value < 0;
  const uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1U
                                      : static_cast<uint64_t>(value);
  const std::optional<internal::WideQuotient> divided = internal::DivideWide(
      internal::MultiplyWide(magnitude, scale.low), divisor.low);
  if (!divided.has_value()) {
    return std::nullopt;
  }
  const bool round_up = internal::RoundsMagnitudeUp(
      rounding, negative, divided->remainder, divisor.low);
  const uint64_t limit = negative ? kMax + 1U : kMax;
  uint64_t result = divided->quotient;
  if (result > limit || (round_up && result == limit)) {
    return std::nullopt;
  }
  if (round_up) {
    ++result;
  }
  return negative ? static_cast<int64_t>(0U - result)
                  : static_cast<int64_t>(result);
}

}

#endif
