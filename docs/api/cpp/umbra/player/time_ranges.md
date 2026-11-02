\file umbra/player/time_ranges.h
Time ranges for `buffered` and `seekable`.

HTML counterpart: `TimeRanges`. Ranges are ordered, do not overlap and are measured in seconds.

\struct umbra::player::TimeRange
One range of media time in seconds.

\var umbra::player::TimeRange::start
The start of the range in seconds.

\var umbra::player::TimeRange::end
The end of the range in seconds.

\class umbra::player::TimeRanges
An ordered list of ranges.

\fn umbra::player::TimeRanges::TimeRanges()
Creates an empty list.

\fn explicit umbra::player::TimeRanges::TimeRanges(std::vector<TimeRange> ranges)
Creates a list from ordered, non-overlapping ranges.

\fn size_t umbra::player::TimeRanges::length() const
Returns the number of ranges.

HTML counterpart: `length`.

\fn double umbra::player::TimeRanges::start(size_t index) const
Returns the start of a range in seconds.

HTML counterpart: `start()`. Deviation: an index at or beyond length() returns NaN, where HTML throws
`IndexSizeError`.

\fn double umbra::player::TimeRanges::end(size_t index) const
Returns the end of a range in seconds.

HTML counterpart: `end()`, with the same deviation as start().
