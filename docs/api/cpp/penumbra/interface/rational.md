\file penumbra/interface/rational.h
Exact fractions and overflow-checked rescaling between time bases.

\enum penumbra::Rounding
Rounding mode for a rescaled value that is not exact.

Rounding is always explicit. Every mode is monotonic, and the result is within one unit of the exact value, or
half a unit for `kNearest`.

\var penumbra::Rounding::kTowardZero
Rounds towards zero.

\var penumbra::Rounding::kAwayFromZero
Rounds away from zero.

\var penumbra::Rounding::kDown
Rounds towards negative infinity.

\var penumbra::Rounding::kUp
Rounds towards positive infinity.

\var penumbra::Rounding::kNearest
Rounds to the nearest value, with ties away from zero.

\class penumbra::Rational
Normalised fraction of two `int64_t` terms, used for time bases, frame rates and aspect ratios.

The denominator is always positive and the fraction is reduced, so equal values have equal terms. The default
value is `0/1`. Create values with Create(), or with Of() for constants.

\fn constexpr penumbra::Rational::Rational()
Creates the fraction `0/1`.

\fn static constexpr std::optional<Rational> penumbra::Rational::Create(int64_t numerator, int64_t denominator)
Creates a normalised fraction, or `std::nullopt` when it cannot be represented.

Returns `std::nullopt` for a zero denominator or when either term is `INT64_MIN`. A negative denominator moves
the sign to the numerator, and both terms are divided by their greatest common divisor.

\fn static consteval Rational penumbra::Rational::Of(int64_t numerator, int64_t denominator)
Creates a constant fraction at compile time.

It follows the rules of Create(), but an invalid fraction fails to compile instead of returning an empty
optional.

\fn constexpr int64_t penumbra::Rational::numerator() const
Returns the numerator, which carries the sign.

\fn constexpr int64_t penumbra::Rational::denominator() const
Returns the denominator, which is always positive.

\fn constexpr bool penumbra::Rational::is_positive() const
Returns whether the value is greater than zero, as every usable time base is.

\fn constexpr bool penumbra::Rational::operator==(Rational, Rational)
Compares two normalised fractions term by term.

\fn std::string penumbra::Rational::ToString() const
Formats the value as `numerator/denominator`, for example `1/90000`.

\fn template <typename Sink> void penumbra::Rational::AbslStringify(Sink& sink, Rational value)
Lets `absl::StrCat`, `absl::StrFormat("%v")` and Abseil logging print the value as ToString() does.

The header does not include Abseil: the hook only calls `sink.Append`.

\fn constexpr std::optional<int64_t> penumbra::Rescale(int64_t value, Rational from, Rational to, Rounding rounding)
Converts `value` from time base `from` to time base `to`, rounding as requested.

Computes `value × from ÷ to`. Returns `std::nullopt` when either time base is not positive, when either term of
the reduced scale `from / to` exceeds `INT64_MAX`, or when the result does not fit in `int64_t`. The scale limit
can reject a conversion whose rounded result would fit: rescaling 1 from `1/INT64_MAX` to `INT64_MAX/1` returns
`std::nullopt`, not 0. Intermediate products use 128-bit arithmetic built from portable 64-bit operations, so
no input overflows or has undefined behaviour, and the function can run in constant expressions.
