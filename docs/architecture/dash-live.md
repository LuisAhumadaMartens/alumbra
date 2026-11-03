# Live DASH

Live DASH timing and refresh are shared by `umbra::manifest` and the adaptive
source. The manifest layer owns UTC source selection, availability-window
calculation, start-position selection and MPD refresh. The adaptive source
waits for the first UTC result before it starts the segment fetcher, then applies
each refreshed immutable presentation to the active streams.

## UTC timing

`ResolveDashUtcOffset` tries the MPD's `UTCTiming` entries in declaration
order. It supports `direct`, `http-xsdate`, `http-iso`, `http-head` and
`http-ntp`. HTTP requests use the injected transport and runner; GET bodies are
limited to 4096 bytes. The result is a duration added to the injected local
wall clock. If the MPD has no timing entry, the configured
`dash.live.default_utc_timing_url` is requested as an HTTP xsdate source. An
empty setting uses the local clock. If every advertised source fails, playback
continues with the local clock so a timing outage does not stop the programme.

## Availability and start position

`DashAvailableWindow` maps corrected UTC through `availabilityStartTime` to
presentation time. A segment is available when its end is no later than the
corrected time plus its inherited `availabilityTimeOffset`. The common edge is
bounded by every resolved track, and `timeShiftBufferDepth` bounds the start of
the window. The segment index is generated only through MPD `publishTime` plus
the declared availability offset, so a future template segment is not queued
before a later refresh advertises it.

`DashLiveStartPosition` uses `dash.live.start_delay_ms` when it is non-negative.
Otherwise it uses `suggestedPresentationDelay`, then three target segment
durations. It clamps the result to the availability window. The adaptive
source also clamps live seeks to that window and reports it as the seekable
range.

## Refresh

`DashMpdRefresher` schedules a manifest GET after `minimumUpdatePeriod`, uses
the first `Location` as the next MPD URL, parses the response and merges it with
`RefreshPresentation`. The response's final URL remains the base for relative
segment references. A refresh error is reported to the callback and retried
after the update period, with a one-second minimum. A static MPD stops the
timer and updates the adaptive source so the fetcher can drain its final
segments and end playback.

The refresh controller owns its timer and request cancellation. Tests use
`TestTaskEnvironment`, the fake HTTP transport and fixed wall clocks; they do
not wait on real time or contact a timing service.
