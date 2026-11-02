# Playback state machine

The playback state machine (#89) is the controller that owns an element's playback state. It runs on the control
sequence ([ADR-0015](../adr/0015-threading-and-task-runners.md)), drives one Penumbra player through the pull-flow
port contract ([ADR-0016](../adr/0016-penumbra-port-contract.md), [port contract](port-contract.md)) and follows the
HTML media element algorithms ([ADR-0023](../adr/0023-public-api-surface-and-media-error.md)). The HTML specification
is the oracle: resource selection, the load algorithm, `play()` and `pause()`, the seeking state, the ready state
change steps, the ended steps and the list of pending play promises. Code comments are not used in Alumbra, so the
rules live here.

The machine is internal. Its headers are in `umbra/pipeline/internal_include/umbra/pipeline/` (target
`umbra::pipeline`, not installed). #95 wraps it in the public `HTMLMediaElement`-shaped API and promotes `PlayResult`
to an installed C++20 type.

## Files

| Header | Contents |
|--------|----------|
| `playback_state_machine.h` | `PlaybackStateMachine`, its `Config` and the `MediaError` record |
| `playback_state.h` | `PlaybackState`, the transition table (`IsLegalTransition`) and `StateTracker` |
| `media_events.h` | `NetworkState`, `ReadyState`, `MediaEventType`, `MediaElementState`, `PlayResult`, `PlayCallback` |
| `event_queue.h` | `EventQueue`, the notify-then-pull queue |
| `sample_source.h` | `SampleSource` and `SampleSink`, the seam to the data sources |

## Threading and lifetime

- Every public method, and every callback from the port or a source, runs on the control sequence. The constructor
  binds a `SequenceChecker`. Nothing blocks.
- The port player is created through `penumbra::PlayerFactory` with a `CallbackDispatcher`. Port callbacks never call
  the machine inline: the receiver posts a new control task (the port contract forbids port calls inside a callback)
  and that task checks the port generation and the event ticket, so events from a replaced player or an older seek
  generation are dropped.
- Sources receive a `SampleSink` that is only valid for the load that created it. A reload, an error or `Shutdown()`
  invalidates it, and later calls are ignored. A source's `Open`, `RequestSamples` and `Seek` may call the sink
  inline. The machine destroys a closed source on a later control task, so it is safe to be torn down from inside
  one of those calls.
- `Shutdown()` (also run by the destructor) follows ADR-0015's order: stop accepting API calls, close the source,
  `Stop()` the port player, stop the dispatcher, then release. The queued events and the callbacks of pending
  `play()` calls are destroyed without running. A `Stop()` that is not yet quiescent (`kUnavailable`) keeps the
  player and dispatcher alive in a retry task (every 10 ms) until it succeeds, as the port contract requires.
- Embedders read attributes through accessors, which reflect the machine immediately (`paused` after `pause()`,
  `currentTime` after a seek). Events are delivered through `EventQueue`: the first entry pushed into an empty queue
  calls the notify hook, outside any lock, and the embedder drains with `Drain()`.

## States

`PlaybackState` is derived from the HTML attributes after every input, and every change goes through the transition
table, so an illegal change asserts in debug builds (`StateTracker::Transition`). The table below also states how each
state maps onto `networkState`, `readyState` and `paused`.

| State | `networkState` | `readyState` | Meaning |
|-------|----------------|--------------|---------|
| `idle` | `NETWORK_EMPTY` | `HAVE_NOTHING` | No resource selected. Initial state, and the state after `Load(nullptr)` or a reset |
| `loading` | `NETWORK_LOADING` | `HAVE_NOTHING` | Resource selected, the source has not reported its streams yet |
| `prerolling` | `LOADING` or `IDLE` | 1 to 3 | Metadata known and the port player exists. Waiting for the first fill to `HAVE_FUTURE_DATA` |
| `ready` | `LOADING` or `IDLE` | 3 or 4 | First preroll finished and `paused` is true. Playback has not been started since |
| `playing` | `LOADING` or `IDLE` | 3 or 4 | `paused` is false and the element is potentially playing |
| `paused` | `LOADING` or `IDLE` | 3 or 4 | `paused` is true after `pause()`, or after a seek, a recovery or playback that was started once |
| `seeking` | unchanged | 1 to 4 | `seeking` is true |
| `buffering` | `LOADING` or `IDLE` | 1 or 2 | Prerolled earlier, not seeking, and `readyState` is below `HAVE_FUTURE_DATA`: an underflow, a recovery or a seek that finished without enough data |
| `ended` | `IDLE` | 3 or 4 | Playback ended, and `paused` is true |
| `error` | `NO_SOURCE` before metadata, otherwise `IDLE` | unchanged | A fatal `MediaError` is set. Only a reload leaves this state |

The derivation order is: error, empty, no metadata, seeking, not yet prerolled, ended, below `HAVE_FUTURE_DATA`,
playing, then ready or paused. `ready` applies only when the previous state was `prerolling` or `ready`.

## Transition table

Any state except `idle` can move to `idle`: `Load()` resets the element first, as the HTML load algorithm does.
Self transitions never happen. The other legal transitions, and what causes them, are:

| From | To | Cause |
|------|----|-------|
| `idle` | `loading` | `Load()` with a source (resource selection, `loadstart`) |
| `loading` | `prerolling` | The source reports its streams and the port player is created |
| `loading` | `error` | The source fails to open, or no stream is playable, or the port cannot create the player |
| `prerolling` | `ready` | `readyState` reaches `HAVE_FUTURE_DATA` while `paused` |
| `prerolling` | `playing` | `readyState` reaches `HAVE_FUTURE_DATA` while not `paused` (`play()` before data, or autoplay) |
| `prerolling` | `seeking` | A seek, or the default start position applied at metadata |
| `prerolling` | `error` | A fatal error |
| `ready` | `playing` | `play()` |
| `ready` | `seeking` | A seek |
| `ready` | `buffering` | The port player is recreated, so data is lost |
| `ready` | `ended` | The port reports that all output drained |
| `ready` | `error` | A fatal error |
| `playing` | `paused` | `pause()` |
| `playing` | `seeking` | A seek, or a loop restart |
| `playing` | `buffering` | Underflow, or a recreated player |
| `playing` | `ended` | The port reports that all output drained and `loop` is false |
| `playing` | `error` | A fatal error |
| `paused` | `playing` | `play()` |
| `paused` | `seeking` | A seek |
| `paused` | `buffering` | The port player is recreated |
| `paused` | `ended` | The port reports that all output drained |
| `paused` | `error` | A fatal error |
| `seeking` | `prerolling` | The seek finished before the first preroll completed |
| `seeking` | `playing`, `paused` | The seek finished with `readyState` at least `HAVE_FUTURE_DATA` |
| `seeking` | `buffering` | The seek finished with `readyState` of `HAVE_CURRENT_DATA` |
| `seeking` | `error` | A fatal error |
| `buffering` | `playing` | Enough data again and not `paused` |
| `buffering` | `paused` | Enough data again and `paused` |
| `buffering` | `seeking` | A seek |
| `buffering` | `ended` | The port reports that all output drained |
| `buffering` | `error` | A fatal error |
| `ended` | `seeking` | `play()` at the end, or a seek |
| `ended` | `error` | A fatal error |

The reset transitions and every row above are exercised by `EveryLegalTransitionIsExercised`,
`LoadFromEveryStateResetsToIdleThenLoading` and the table test, which compares `IsLegalTransition` with an
independent copy of this table. Every transition is also reported to `Config::on_transition` as a
`StateTransition`; #50's trace macros will subscribe to that hook when they land.

## Port contract assumptions

The Penumbra `Player` has no play or pause call, and `PlayerState` only has `kReady`, `kPresenting` and
`kEndOfStream`. The machine therefore uses these readings, which #71's conformance kit should confirm:

- A new player holds the clock at rate 0 until `SetPlaybackRate` is called. The machine sets 0 right after creation.
- Pausing, buffering, seeking and ending hold the port with `SetPlaybackRate(0)`. Playing sets the element's
  `playbackRate`. The machine only changes the port rate when the wanted value differs from the applied one.
- `kReady` means the port holds presentable data and the clock is not advancing: after a seek or a start, and when
  output starves. `kPresenting` means the clock advances. A track-less `kEndOfStream` means every stream drained, and
  per-track `kEndOfStream` events are ignored.
- A `kReady` event while the clock is presenting, the element is not paused, and no rate command was sent since the
  last `kPresenting`, is an **underflow**. A `kReady` caused by the machine's own rate 0 is not.
- Buffering holds the port at rate 0, so the machine, not the port, decides when to resume: playback restarts when
  `readyState` is back at `HAVE_FUTURE_DATA`, which needs the preroll amount of data after a stall.

Writes follow the pull flow: `NeedsData{track, max}` is forwarded to the source with the current ticket, and the
source answers with at most `max` samples or an end of stream, which the machine writes with the current ticket.
Answers with an old ticket, or without an outstanding grant, are dropped.

## `readyState`

`readyState` is the minimum over the selected audio and video tracks, as ADR-0023 decides. It is computed from the
machine's own accounting: the end of the media written to the port per track, and the position.

| Value | Condition |
|-------|-----------|
| `HAVE_NOTHING` | No metadata |
| `HAVE_METADATA` | The port has not reported `kReady` for this generation. Every seek, flush and recreation starts here |
| `HAVE_CURRENT_DATA` | The port is ready, and the lowest track has less than `playback.preroll_ms` of data ahead of the position, or the output underflowed since the last write |
| `HAVE_FUTURE_DATA` | The lowest track has at least `playback.preroll_ms` ahead |
| `HAVE_ENOUGH_DATA` | The lowest track has at least twice `playback.preroll_ms` ahead. A track whose end of stream was accepted counts as unlimited |

`playback.preroll_ms` is an integer from 0 to 10000. The machine's default is 500 ms until #2 and the settings schema
(#7) fix the default. `PlaybackStateMachine::ValidateConfig` rejects values outside the range. The doubled amount for
`HAVE_ENOUGH_DATA` is a placeholder until ABR (#11) can say when playback can run through.

## Events

Events are queued as they are fired, each with the attribute values at that moment (`MediaElementState`), and a play
callback is queued as a `PlayCompletion` between them. The order below is what the tests assert and what HTML's
algorithms give.

| Operation | Queue order |
|-----------|-------------|
| `Load(source)` from a loaded element | `play` rejections (`kAbort`) at once, then `abort` (if `LOADING` or `IDLE`), `emptied`, `timeupdate` (if the position was not 0), `ratechange` (if the rate changed to the default), `loadstart`. Events that were queued and not yet pulled are removed, play completions stay |
| Metadata | `durationchange` (if known), `loadedmetadata` |
| `readyState` rises | `loadedmetadata`, `loadeddata` (first time since load), `canplay` and `playing` with the resolved promises (when it passes `HAVE_FUTURE_DATA` and `paused` is false), `canplaythrough`, then autoplay `play` and `playing` |
| `readyState` drops from `HAVE_FUTURE_DATA` or more | `timeupdate`, `waiting`, only if the element was potentially playing |
| `play()` | `play`, then `waiting` below `HAVE_FUTURE_DATA`, otherwise `playing` followed by the resolved promises. Already playing: only the resolution. At the end it seeks to 0 first |
| `pause()` | `timeupdate`, `pause`, then the pending promises reject with `kAbort` |
| Seek | `seeking`, the `readyState` drop above, and when data is back `timeupdate`, `seeked`, then the `readyState` rise above |
| Fetch completed (end of stream accepted on every track) | `progress`, `suspend`, `networkState` becomes `IDLE` |
| End of playback | `timeupdate`, `pause` and `kAbort` rejections (only if not paused), `ended`. With `loop` it seeks to 0 instead |
| Fatal error | `error`. Before metadata the pending promises then reject with `kNotSupported` |
| Rate, volume | `ratechange`, `volumechange` |

`PlayResult` has the codes of ADR-0023: `kOk`, `kNotAllowed` (the `Config::allow_playback` hook refuses),
`kNotSupported` (the element failed with `MEDIA_ERR_SRC_NOT_SUPPORTED`) and `kAbort`. A completion is always queued,
never run inline. `ratechange` follows a new value of `playbackRate` or `defaultPlaybackRate`, and `load()` resets
`playbackRate` to `defaultPlaybackRate`.

## Errors and recovery

The machine owns the part of the recovery ladder of [ADR-0018](../adr/0018-error-model-and-recovery.md) that concerns
the port. Retries, failover and rendition exclusion belong to the sources (#127).

- A port error with `kTransient` is telemetry only. #50 will receive it.
- A port error with `kRecreate`, or a failed port `Seek` or an unsupported in-band configuration change, enters the
  ladder at step 4: the machine stops and releases the player, creates a new one with a larger ticket, and seeks to
  the last position. `readyState` drops, so `waiting` fires if the element was playing. After
  `Config::max_recreate_attempts` (default 2) recreations without an intervening `kPresenting`, or if the new player
  cannot be created, the error is fatal.
- A fatal port error, source failure, or player creation failure sets `error`, closes the source, stops the player
  and fires `error`. A status that carries an `ErrorDetail` payload keeps it. Otherwise the machine builds a detail
  from the port error category: `kDecode` gives `kMediaCorrupt`, `kCapability` gives `kCodecUnsupported`, other
  categories give `kPlayerInternal`. A creation failure gives `kCodecUnsupported` (`kUnimplemented`,
  `kInvalidArgument`) or `kDecoderInitializationFailed`.
- Before metadata the `MediaError` code is always `MEDIA_ERR_SRC_NOT_SUPPORTED` (4) and `networkState` becomes
  `NETWORK_NO_SOURCE`, as HTML's media element failure steps say. After metadata the code is the catalogue's
  `media_error` for the detail, and `networkState` becomes `NETWORK_IDLE`.
- Degraded and recoverable source errors do not stop playback. They are ignored until #96 raises warning events.

## Deliberate differences from the HTML algorithms

- `load()` resets `readyState` to `HAVE_NOTHING` without firing the ready state change events, as browsers do.
- Every seek drops `readyState` to `HAVE_METADATA` until the flushed port is ready again, because the machine does
  not know the port's buffered ranges. Seeking inside buffered data therefore fires `canplay` after `seeked`.
  Seeking modes and buffered-range checks are #91.
- Event attribute values are captured when an event is queued, not when a task would run.
- `playbackRate` accepts finite non-negative values only: reverse playback is out (ADR-0024, port contract).
  The public error mapping is #95.

## Deferred

| Behaviour | Issue |
|-----------|-------|
| Public API, `PlayCallback` and `PlayResult` as installed types, `readyState`/`networkState` accessors on the element, `MediaError` object, `preload`, `defaultPlaybackStartPosition` via `#t=`, `canPlayType()`, `srcObject`, `<source>` children | #95 |
| `suspend` with `preload`, `stalled`, the 350 ms `progress` cadence, the 250 ms `timeupdate` cadence and coalescing, the typed `AddEventListener`, the warning event, the full catalogue-to-`MediaError` mapping and message | #96 |
| `fastSeek()`, seek to buffered ranges without a flush, seekable ranges other than `[0, duration]`, live windows | #91 |
| Clock start on the first audio sample, `preservesPitch`, rate muting outside 0.25 to 4 (port and clock work; the machine only sets the rate) | #90, #92 |
| Fragment pass-through (`WriteFragment`), text streams, `buffered` ranges as a public object | #95, sources |
| Wiring `on_transition` and the recoverable errors to `ALUMBRA_TRACE` and telemetry | #50 |
| The preroll default and the settings schema entry | #2, #7 |
| Retries, failover and rendition exclusion (ladder steps 1 to 3) | #127 |

## Tests

`test/umbra/pipeline/` runs the machine on `TestTaskEnvironment` against `penumbra::test::FakePort`, with
`FakeSampleSource` standing in for a data source (it can hold deliveries to inject a stall). No test sleeps. The
suites cover the transition table, the queue, every state and transition, the event orders above, the play
bookkeeping, seek, loop and end, stall and recovery, port recreation, fatal errors, reload from every state, and
destruction in every state (including a `Stop()` that fails twice). CI runs them under ASan and TSan with the rest
of the unit tests.
