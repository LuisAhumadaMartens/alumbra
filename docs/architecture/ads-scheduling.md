# Ad scheduling

`umbra::ads` owns the client-side ad break schedule. Providers supply pods and
report playback progress; the manager owns pause, content snapshots, track
restoration, snapback and the deferred content `ended` notification. It uses a
content controller so provider adapters do not duplicate those decisions.

Break offsets resolve against the finite content duration. Start and end
offsets represent pre-roll and post-roll; absolute offsets are seconds and
percentage offsets range from 0 through 100. Cue points are available as
resolved content times for seek-bar markers. VMAP accepts `start`, `end`,
`hh:mm:ss.mmm`, percentages and positional `#n` offsets. Positional breaks are
honoured only when the VMAP has no timed offsets; because this scheduler
requires content times, positional ordinals are spaced proportionally over the
finite content duration. `repeatAfter` expands a linear break up to the content
duration. Non-linear and invalid breaks are omitted; invalid breaks are
returned as parser errors. Other valid breaks remain in the schedule. A VAST
document without a VMAP root
becomes one pre-roll. Inline VAST is parsed by the VAST parser, while AdTagURI
metadata is retained for the provider.

Break sources are requested once when the content clock enters the configured
`ads.prefetch_lead_ms` window. The default lead is 5000 ms and the allowed range
is 0–60000 ms. The settings integration supplies this value through
`AdManagerOptions::prefetch_lead`. Tracking URIs for `breakStart`, `breakEnd` and `error` are sent
through the manager's tracking callback in lifecycle order.

The `ads.seek.play_skipped_breaks` setting selects `last`, `all` or `none` for
breaks crossed by a forward seek. Crossed breaks not selected by the policy are
marked played so playback does not unexpectedly return to them. Completed
breaks are not replayed unless the manager is configured to replay played
breaks.

Provider preparation, start and progress failures, as well as provider timeouts,
emit an ad error and finish the break. The manager resumes the saved content
position and selected tracks. Ad failures do not enter the content media-error
path. An empty pod is treated as a completed break.

The scheduler is deterministic: callers pass media-clock times into content
notifications and `Tick()`. No wall clock or platform header is used.
