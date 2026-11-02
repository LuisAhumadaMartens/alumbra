\file penumbra/interface/media_time.h
Microsecond durations and media positions, exact container timestamps, and conversions between them.

TimeDelta and MediaTime hold `int64_t` microseconds, the unit that Starboard, WebCodecs, MediaCodec and FFmpeg
use, and reserve three sentinels: `INT64_MIN` is unknown, `INT64_MIN + 1` is negative infinity and `INT64_MAX`
is positive infinity, so finite values lie strictly between. Arithmetic saturates to the infinities instead of
overflowing, unknown propagates, and infinity minus infinity is unknown. Timestamp keeps a container's exact
value and time base. Every conversion takes an explicit Rounding and returns `std::nullopt` on overflow.

\include convert_timestamps.cc

\class penumbra::TimeDelta
Signed duration in microseconds, with unknown and infinite sentinels.

The default value is zero.

\fn constexpr penumbra::TimeDelta::TimeDelta()
Creates the zero duration.

\fn static constexpr TimeDelta penumbra::TimeDelta::FromMicroseconds(int64_t microseconds)
Creates a duration from microseconds.

Values beyond the finite range, including `INT64_MIN`, saturate to an infinity, so this function never returns
the unknown sentinel.

\fn static constexpr TimeDelta penumbra::TimeDelta::FromMilliseconds(int64_t milliseconds)
Creates a duration from milliseconds, saturating to an infinity outside the finite range.

\fn static constexpr TimeDelta penumbra::TimeDelta::FromSeconds(int64_t seconds)
Creates a duration from seconds, saturating to an infinity outside the finite range.

\fn static constexpr TimeDelta penumbra::TimeDelta::Zero()
Returns the zero duration.

\fn static constexpr TimeDelta penumbra::TimeDelta::Unknown()
Returns the unknown duration.

\fn static constexpr TimeDelta penumbra::TimeDelta::Infinite()
Returns positive infinity.

\fn static constexpr TimeDelta penumbra::TimeDelta::NegativeInfinite()
Returns negative infinity.

\fn constexpr bool penumbra::TimeDelta::is_unknown() const
Returns whether the duration is unknown.

\fn constexpr bool penumbra::TimeDelta::is_infinite() const
Returns whether the duration is positive or negative infinity.

\fn constexpr bool penumbra::TimeDelta::is_finite() const
Returns whether the duration is neither unknown nor infinite.

\fn constexpr int64_t penumbra::TimeDelta::InMicroseconds() const
Returns the microsecond count, including the sentinel values.

\fn constexpr auto penumbra::TimeDelta::operator<=>(TimeDelta, TimeDelta)
Orders durations as unknown, negative infinity, finite values, then positive infinity.

\fn constexpr TimeDelta penumbra::TimeDelta::operator+(TimeDelta a, TimeDelta b)
Adds two durations, saturating to an infinity on overflow.

Unknown plus any duration is unknown, and the sum of opposite infinities is unknown.

\fn constexpr TimeDelta penumbra::TimeDelta::operator-(TimeDelta a, TimeDelta b)
Subtracts `b` from `a` with the saturation and sentinel rules of addition.

\fn constexpr TimeDelta penumbra::TimeDelta::operator-(TimeDelta value)
Negates a duration; the infinities swap and unknown stays unknown.

\fn std::string penumbra::TimeDelta::ToString() const
Formats the duration as microseconds with a `us` suffix, or as `+inf`, `-inf` or `unknown`.

\fn template <typename Sink> void penumbra::TimeDelta::AbslStringify(Sink& sink, TimeDelta value)
Lets Abseil formatting and logging print the duration as ToString() does.

\class penumbra::MediaTime
Position on the media timeline in microseconds, with unknown and infinite sentinels.

Subtracting two positions gives a TimeDelta, and adding a TimeDelta to a position gives a MediaTime; adding two
positions does not compile. The default value is position zero.

\fn constexpr penumbra::MediaTime::MediaTime()
Creates position zero.

\fn static constexpr MediaTime penumbra::MediaTime::FromMicroseconds(int64_t microseconds)
Creates a position from microseconds.

Values beyond the finite range, including `INT64_MIN`, saturate to an infinity, so this function never returns
the unknown sentinel.

\fn static constexpr MediaTime penumbra::MediaTime::FromMilliseconds(int64_t milliseconds)
Creates a position from milliseconds, saturating to an infinity outside the finite range.

\fn static constexpr MediaTime penumbra::MediaTime::FromSeconds(int64_t seconds)
Creates a position from seconds, saturating to an infinity outside the finite range.

\fn static constexpr MediaTime penumbra::MediaTime::Zero()
Returns position zero.

\fn static constexpr MediaTime penumbra::MediaTime::Unknown()
Returns the unknown position.

\fn static constexpr MediaTime penumbra::MediaTime::Infinite()
Returns positive infinity.

\fn static constexpr MediaTime penumbra::MediaTime::NegativeInfinite()
Returns negative infinity.

\fn constexpr bool penumbra::MediaTime::is_unknown() const
Returns whether the position is unknown.

\fn constexpr bool penumbra::MediaTime::is_infinite() const
Returns whether the position is positive or negative infinity.

\fn constexpr bool penumbra::MediaTime::is_finite() const
Returns whether the position is neither unknown nor infinite.

\fn constexpr int64_t penumbra::MediaTime::InMicroseconds() const
Returns the microsecond count, including the sentinel values.

\fn constexpr auto penumbra::MediaTime::operator<=>(MediaTime, MediaTime)
Orders positions as unknown, negative infinity, finite values, then positive infinity.

\fn constexpr MediaTime penumbra::MediaTime::operator+(MediaTime time, TimeDelta delta)
Moves a position forward by a duration, with the saturation and sentinel rules of TimeDelta.

\fn constexpr MediaTime penumbra::MediaTime::operator-(MediaTime time, TimeDelta delta)
Moves a position back by a duration, with the saturation and sentinel rules of TimeDelta.

\fn constexpr TimeDelta penumbra::MediaTime::operator-(MediaTime a, MediaTime b)
Returns the duration from `b` to `a`, with the saturation and sentinel rules of TimeDelta.

\fn std::string penumbra::MediaTime::ToString() const
Formats the position as TimeDelta::ToString() does, prefixed with `@`.

\fn template <typename Sink> void penumbra::MediaTime::AbslStringify(Sink& sink, MediaTime value)
Lets Abseil formatting and logging print the position as ToString() does.

\struct penumbra::Timestamp
Exact container timestamp: an integer value in a rational time base, for example `3003` in `1/90000`.

Samples keep their container timestamps unchanged, so remuxing and gapless trimming never accumulate rounding
errors. Convert them with ToMediaTime(), ToTimeDelta(), ToTimestamp() and Rescale().

\var static constexpr int64_t penumbra::Timestamp::kUnknown
Value of a missing timestamp, `INT64_MIN`, which converts to the unknown sentinel.

\var int64_t penumbra::Timestamp::value
Value in units of `time_base`; kUnknown by default.

\var Rational penumbra::Timestamp::time_base
Seconds per unit of `value`.

The default, `0/1`, is not positive, so a known value with it cannot be converted.

\fn constexpr bool penumbra::Timestamp::is_known() const
Returns whether `value` is not kUnknown.

\fn constexpr bool penumbra::Timestamp::operator==(const Timestamp&, const Timestamp&)
Compares value and time base, so the same instant in two time bases compares unequal.

\fn std::string penumbra::Timestamp::ToString() const
Formats the timestamp as the value, or `unknown`, followed by `@` and the time base, for example `3003@1/90000`.

\fn template <typename Sink> void penumbra::Timestamp::AbslStringify(Sink& sink, const Timestamp& value)
Lets Abseil formatting and logging print the timestamp as ToString() does.

\var penumbra::kMicrosecondTimeBase
The `1/1000000` time base of TimeDelta and MediaTime.

\fn constexpr std::optional<MediaTime> penumbra::ToMediaTime(Timestamp timestamp, Rounding rounding)
Converts a timestamp to a media position.

An unknown timestamp becomes MediaTime::Unknown(). Returns `std::nullopt` when a known timestamp cannot be
rescaled to microseconds, for example because its time base is not positive, or when the result is outside the
finite range.

\fn constexpr std::optional<TimeDelta> penumbra::ToTimeDelta(Timestamp duration, Rounding rounding)
Converts a timestamp that holds a duration, such as a sample duration, with the rules of ToMediaTime().

\fn constexpr std::optional<Timestamp> penumbra::ToTimestamp(MediaTime time, Rational time_base, Rounding rounding)
Converts a media position to a timestamp in `time_base`.

The unknown position becomes a timestamp whose value is Timestamp::kUnknown. Returns `std::nullopt` for a time
base that is not positive, an infinite position, or a result that overflows or equals Timestamp::kUnknown.

\fn constexpr std::optional<Timestamp> penumbra::ToTimestamp(TimeDelta duration, Rational time_base, Rounding rounding)
Converts a duration to a timestamp in `time_base`, with the rules of the MediaTime overload.

\fn constexpr std::optional<Timestamp> penumbra::Rescale(Timestamp timestamp, Rational time_base, Rounding rounding)
Expresses a timestamp in another time base.

An unknown timestamp stays unknown in the new time base. Returns `std::nullopt` when `time_base` is not positive,
or when the value cannot be rescaled or would equal Timestamp::kUnknown.
