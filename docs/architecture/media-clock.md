# Media clock components

ADR-0024 assigns shared clock and frame-selection policy to the Penumbra
reference player. The reusable timing components live in
`penumbra/reference_player/` and take monotonic time as an explicit argument,
so unit tests can drive them without sleeps or a platform clock.

## Media clock

`MediaClock` anchors media time to monotonic time, playback rate and resample
ratio. It freezes while paused and clamps reads to a monotonic value between
resets. Audio updates carry the media position of the most recently rendered
sample, its observation time, output latency and the configured audio offset.
The clock interpolates between updates and bounds its correction speed. A
large forward difference re-anchors immediately; a backward difference holds
the clock until audio catches up. A latency or offset change can be slewed over
an explicit interval.

The state is published through atomic fields and a sequence counter so readers
do not take a mutex. Mutations belong to the player's control sequence. A seek
uses `Reset` and is the only operation that permits media time to move
backwards.

## Cadence and frame selection

`VideoCadenceEstimator` searches repeating patterns of up to five refreshes per
frame. It prefers the lowest average drift, compacts repeating patterns, and
requires a candidate to remain unchanged for two seconds before switching.
This keeps boundary jitter from changing the pattern on every refresh.

`VideoFrameSelector` picks the newest due frame that is no more than the
configured percentage of one frame interval late. Older frames beyond that
threshold are counted as drops. The default threshold is 100 percent, matching
`playback.sync.frame_drop_lateness_percent` in the settings schema. The caller
removes the dropped prefix from its frame queue and keeps audio consumption
independent of video selection.

These components provide the clock, cadence and late-frame policy. The
platform player's audio callback, vsync deadline source, decoder keyframe-skip
request and snapshot publication remain integration points for the full
reference-player path. Platform present timing is also needed before desktop
display resampling can be enabled, as ADR-0024 and ADR-0030 specify.
