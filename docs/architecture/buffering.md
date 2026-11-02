# Buffer goals and sample accounting

[ADR-0034](../adr/0034-buffer-model-and-memory-budgets.md) sets time goals and byte limits for the loader and
sample queue. `BufferManager` is the scheduler boundary in `umbra::pipeline`: every `NeedsData` grant is checked
against the forward goal and the per-media-type byte estimate before Umbra asks a source for samples. The port's
grant remains the maximum write size.

## Configuration

The `buffer.*` schema is in [`settings/schema/buffer.toml`](../../settings/schema/buffer.toml). Its live values map
to the same-named `BufferManager::Config` fields. `SetBufferConfig` applies a validated replacement immediately;
the next `NeedsData` decision observes it. A port pressure notification is posted to the control sequence and
reduces the active byte caps and forward goal while the signal is active. Moderate pressure uses three quarters
of each value and critical pressure uses one half, with a one-second minimum forward goal.

The initial defaults are 30 seconds forward, 10 seconds behind, a one-second start threshold and a four-second
rebuffer threshold. The provisional desktop caps are 64 MiB video and 16 MiB audio. They are configuration
starting points; constrained ports can supply lower config values.

## Accounting and eviction

The ledger records each accepted sample's media interval and owned byte length by track, then totals audio and
video independently. It estimates the next grant's bytes from the highest observed per-track byte rate. A grant is
held when its projected bytes exceed the cap, except while the buffered forward range is below the rebuffer
threshold; the port still limits the resulting write to its grant. Samples behind the configured back goal are
evicted first, retaining the last keyframe at or before the cutoff. If that does not reach the active cap, the
manager removes the furthest samples beyond the rebuffer threshold. Samples at or after the playhead within that
threshold are protected.

The current Penumbra sample API has no callback reporting sample release or a method to evict samples already
written to a port. The ledger therefore tracks its scheduling estimate, but cannot prove allocator residency or
free samples already held inside a port. An actual port quota response and HLS/DASH segment-level admission need
the segment scheduler and quota integration work described by ADR-0034.

## State and events

The start threshold controls the initial transition to `HAVE_FUTURE_DATA`; the rebuffer threshold controls the
same transition after playback has begun. Existing `readyState` transitions continue to derive `waiting`,
`canplay` and `canplaythrough` in one place in `PlaybackStateMachine`. The configured forward goal caps further
source requests independently of the port's needs-data grants.

## Tests

`BufferManagerTest` covers accounting, the forward goal, byte caps, the rebuffer exception, pressure reductions,
back-buffer eviction and live config changes. Pipeline tests use the fake port and fake source, with no wall-clock
delays.
