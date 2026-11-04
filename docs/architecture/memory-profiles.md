# Memory profiles

`perf.profile` selects `auto`, `standard` or `low_memory`. `auto` selects the
low-memory profile when a platform adapter supplies a low-RAM flag, total
physical memory at or below 1 GiB, or a browser memory estimate at or below
1 GiB. When no signal is supplied, it selects `standard`. Platform adapters
pass these values into the pipeline; Umbra does not include Android, Apple or
browser headers.

The low-memory profile starts with a 10-second forward goal, a 2-second back
goal, a 32 MiB video sample budget and a 4 MiB audio sample budget. These
starting values are provisional until the device budget work in #263 measures
them. Configured buffer values can lower these limits. The profile caps video
height at `perf.low_memory.max_video_height`, whose provisional default is
1080 pixels. Moderate and critical memory pressure lower the maximum height to
720 and 480 pixels respectively; a normal-pressure signal restores the
profile's limit. BufferManager independently trims its back buffer and reduces
its active byte budgets and forward goal.

`PlaybackStateMachine::MaxVideoHeight()` exposes the current cap for ABR
callers. The profile policy is integrated with the existing buffer manager and
Penumbra pressure observer. A platform adapter must still supply the device
signals, and the ABR caller must apply the exposed cap before selection.

The current port sample contract cannot evict samples already owned by a
decoder or renderer, and this branch has no thumbnail, surface or texture cache
trim interface. Pressure handling therefore trims the engine's buffer ledger
and reduces future quality and buffering without stopping playback; it cannot
claim that platform allocations have been released. Android lifecycle
callbacks, Apple warning notifications, browser quota retry, ad-pipeline
changes and the 1 GB device soak remain platform or follow-up work.
