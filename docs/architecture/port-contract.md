# Penumbra interface v1

[ADR-0016](../adr/0016-penumbra-port-contract.md) chooses a push-model **player**: Umbra owns networking,
demuxing and ABR; the port owns decode, pacing, presentation, synchronisation and statistics. A shared reference
player will implement that contract from decoders and sinks. These headers define both paths, not the reference
player implementation. The conformance kit (#71) is described in the [porting guide](porting-guide.md).

## Placement and versioning

`penumbra::interface` remains header-only. `include/penumbra/interface/` contains installed C++20-only media and
contract value types (`port_types.h` and `interface.h`). The abstract port interfaces and callback dispatcher
live in **non-installed** `internal_include/penumbra/interface/`: they use `absl::Status`, `absl::StatusOr` and
`absl::AnyInvocable` under ADR-0018/ADR-0013. In-tree ports and Umbra see both directories through the same
target. No Penumbra header includes Umbra or platform headers.

This is the existing [public-header boundary](overview.md#public-headers), not an exception for Abseil in an
installed header. The layering allowlist now checks the installed and internal directories separately; the
internal directory permits only standard headers, Abseil and Penumbra interface headers. Abseil links and
internal include paths are build-only, never exported to installed consumers.

`penumbra::kApiVersion` is **1**. `PlayerConfig.api_version` and
`PortLimits.api_version` must match it. Reject a mismatch with `kInvalidArgument`; never guess a compatible
layout. A source-incompatible change (including a new pure virtual method, changed ownership/thread semantics
or changed enum meaning) increments the integer. Additive optional value fields may keep the version only when
their defaults preserve old behaviour. Enum values cannot be reused or silently reinterpreted. There is no
stable binary ABI: core and ports rebuild together with one toolchain. An out-of-tree installed port contract
requires a separately reviewed packaging/ABI decision.

## Common call contract

Unless a table overrides it:

- **Sequence:** all factory, capability, player, decoder, sink and observer-registration calls run on the
  player's control sequence (a port instance has one owning control sequence). Never block that sequence.
  Port implementations use their platform workers for blocking operations. Capability queries are synchronous,
  bounded probes, not a request to open a decoder or perform I/O. Umbra caches results; speed measurement belongs
  to #71, not this change.
- **Reentrancy:** no port method may be called inside any port callback, including after a callback has been
  marshalled. Post a new control task instead. `CallbackScope` marks delivery, and `PortCallScope` asserts against
  reentrancy in debug builds. Ports must apply those scopes at their callback and method boundaries. This is
  distinct from ADR-0015's reentrant *public SDK event handlers*, whose calls only post work.
- **Borrowing:** references, spans and string views last only until the call returns. Copy value descriptors to
  retain them; retain `SharedBytes` to share immutable sample/fragment bytes without copying. Factories consume
  configuration values and return exclusive instance ownership. Callback/source/observer shared pointers retain
  the recipient through shutdown; never create an ownership cycle back to the port.
- **Errors:** no exceptions. Fallible calls return canonical `absl::Status`/`StatusOr`; non-OK results never
  masquerade as success. `kInvalidArgument` means invalid input, `kFailedPrecondition` means lifecycle/flow misuse,
  `kResourceExhausted` means a platform instance/quota limit, `kUnavailable` means a transient platform failure,
  and `kUnimplemented` means an optional unsupported path. A successful unsupported capability query returns
  `supported=false` (or `false` for `CanChangeType`), rather than an error. Runtime errors use `PortError` with
  a non-OK Status, category and transient/recreate/fatal hint. Existing Status payloads must survive the boundary.
  This does not define another catalogue or `ErrorDetail`; [the shared error model](errors.md) from #49
  supplies those helpers/codes. The Penumbra headers remain independent of Umbra.
- **Stop:** terminal, idempotent and nonblocking. Success is the barrier after which no new callback/source
  invocation can begin and every in-progress invocation has completed. A port whose platform needs asynchronous
  quiescence returns `kUnavailable` until it is quiescent; it must not wait on the control sequence. Retry Stop
  on later control tasks. On failed Stop retain the instance and its recipients. Destroy only after successful
  Stop. The platform implementation must cancel work and synchronise the barrier without holding locks while
  invoking recipients.

## Tickets, flow control and callback delivery

`SeekTicket` is an unsigned 64-bit generation, **nonzero initially**. Every Seek/Flush must carry a strictly
larger value. The caller and port never wrap it: at exhaustion stop and recreate the instance. Writes,
fragments, EOS and events carry the current ticket. Discard older tickets with OK, without consuming a grant,
retaining data or publishing an event. Reject future tickets with `kInvalidArgument`. Failed Seek/Flush leaves
the port generation unchanged. Flush clears pending data, decoded queues, grants and EOS state but preserves
the media position; Seek also resets the clock to the requested finite position. Counters reset with a generation.

`NeedsData{track_id,max_samples}` grants **one** write or EOS for that stream and ticket. The maximum is positive;
there may be only one outstanding grant per stream. An accepted write of 1..max samples consumes the whole grant,
even if smaller than the maximum. Invalid writes do not consume it. Reject unsolicited writes with
`kFailedPrecondition`. Stop and successful Seek/Flush invalidate every grant. EOS consumes a grant, is per stream,
and forbids subsequent requests/writes for that stream until the next generation. `StateChanged(kEndOfStream,
track_id)` reports that stream's output drained, not just that input EOS was accepted; a player-wide EOS event
has no track and occurs only after all configured streams drain.

Callbacks can originate on **any platform thread**, never under an internal lock, and cannot re-enter the port.
`PlayerCallbacks::OnEvent` returns an observable delivery Status. A receiver on a platform thread must only
enqueue it on Umbra's control sequence, not perform engine work there.

`CallbackDispatcher` is the in-tree player receiver. It posts an owned event through `ControlTaskRunner` and
checks its shared atomic generation both on enqueue and on delivery; a callback posted before a seek but delivered
after it is discarded. It never captures the dispatcher's `this`, so queued tasks survive wrapper destruction
without dangling pointers. The recipient lives through those tasks.

Advance the dispatcher ticket **before** calling Seek/Flush, so synchronous callbacks for the new ticket are
not lost. Validate the requested ticket/position first. If the port fails the transition, stop/quiesce the
instance and dispatcher and recreate it; do not roll the dispatcher back to an old generation. On shutdown,
successfully Stop the port, Stop the dispatcher, then drain the control sequence and release resources. Queued
deliveries are then no-ops. Stop/AdvanceTicket and delivery serialise on the control sequence; arbitrary platform
threads may only call `OnEvent` or read `GetDeliveryStatus`. Do not shut down the runner before quiescing the port.

| Call | Sequence, lifetime/ownership and result |
|------|----------------------------------------|
| `PlayerCallbacks::OnEvent` | Any platform thread; transfers one event to the recipient, which must marshal it. Non-OK means enqueue/delivery failed; the caller must report or terminate, not ignore it. No callbacks after successful Stop. |
| `CallbackDispatcher` constructor | Control sequence; nonzero ticket and non-null runner/target are required and debug-asserted; retains both shared recipients. |
| `CallbackDispatcher::OnEvent` | Any thread; stale/stopped events are deliberately discarded with OK, future tickets give `kInvalidArgument`; returns the runner's enqueue failure unchanged. Never runs target inline. |
| `CallbackDispatcher::AdvanceTicket` | Control sequence; strictly increasing ticket, `kInvalidArgument` otherwise; stopped gives `kFailedPrecondition`. Invalidates already queued older events. |
| `CallbackDispatcher::Stop` | Control sequence; terminal/idempotent suppression of queued and future delivery, no callbacks; release only after port Stop succeeds. |
| `CallbackDispatcher::GetDeliveryStatus` | Any thread; returns the most recent non-OK target-delivery Status, including its payload. OK means no observed target failure, not that pending tasks have run. The owner must inspect it after drains; enqueue failures are returned directly by OnEvent. |
| `CallbackScope` construction/destruction | Callback thread; scoped thread-local marker, no ownership transfer; nestable and non-copyable. |
| `PortCallScope` construction | Port-call thread; debug assertion on callback reentrancy; no release-build enforcement or ownership. |

## Player and capabilities

`StreamConfig` carries #51's `StreamInfo`, an optional `cenc`/`cbcs` scheme and video output mode. Encryption
metadata stays in `EncodedSample`, including key ID, IV, subsamples and pattern. `PlayerConfig.video_ceiling`
passes ABR maxima before allocation (width/height, frame rate and bitrate; zero means unspecified). This is a
request-specific description, not a new runtime setting. Query capabilities before choosing output/input modes.

Sample writes share codec configuration, HDR metadata and compressed bytes through #51's ownership types.
`config_change` is an in-band stream reconfiguration, retaining the same track ID. `CanChangeType` decides whether
that change can happen without recreating the player. Unsupported changes fail with `kUnimplemented` and do
not consume a grant.

Fragment pass-through is selected for the **whole player**, only when every stream supports it and no transform
is needed. `MediaFragment` retains original fMP4/CMAF bytes, participating track IDs, parsed timing, duration and
an initialisation-fragment flag. Initialisation fragments may have unknown timing. A multiplexed fragment uses
one selected participating track's grant, counts as one item, and must not also be sent under the other tracks'
grants. The port must pace grants to prevent duplicate multiplexed appends; Umbra still parses for timing/ABR.

| Call | Sequence, lifetime/ownership and result |
|------|----------------------------------------|
| `PlayerFactory::GetLimits` | Control; returns a snapshot, at least one max player and current API version. A platform failure is non-OK, not a zero limit. |
| `PlayerFactory::CreatePlayer` | Control; consumes valid distinct stream configs and retains non-null callbacks; returns exclusive player. No callbacks before return. Negotiated modes/version/ticket must be valid; unsupported codec/path gives `kUnimplemented`, instance exhaustion gives `kResourceExhausted`. Failed creation retains no callbacks. |
| `CapabilityProvider::QueryDecoding` | Control; borrows MIME (including codec parameters), stream profile/level via RFC 6381 codec string, geometry/frame rate, bitrate, HDR format, key system and encryption scheme. Returns supported/smooth/power-efficient, preferred per-stream output mode and fragment support. Unknown support is false; a probe failure is non-OK. No allocation of player resources. |
| `CapabilityProvider::CanChangeType` | Control; borrows old/new stream descriptions, including encryption and output; returns whether an in-place config-change write is supported. Does not modify either or perform the change. Probe errors are non-OK. |
| `Player::WriteSamples` | Control; borrows a nonempty same-track span and retains accepted sample descriptors, never copies compressed bytes. Current ticket and one grant required. Validate track/encryption/config-change before consuming grant; errors follow the common contract. Release retained input when consumed, flushed or stopped. |
| `Player::WriteFragment` | Control; borrows descriptor, retains its SharedBytes after acceptance. Fragment mode and participating requested track required; one selected-track grant is consumed. Reject malformed/unknown/ended tracks with `kInvalidArgument`. |
| `Player::EndOfStream` | Control; ticket plus track, no borrowed data. Consumes one grant; input closes, output-drained event follows asynchronously. Stale EOS has no effect; duplicate/current EOS without grant gives `kFailedPrecondition`. |
| `Player::Seek` | Control; strictly larger ticket and finite media position; invalid input gives `kInvalidArgument`. Accept resets queues, grants, EOS, counters and media clock without waiting for platform decode. Later failure is an error callback. |
| `Player::Flush` | Control; same generation/reset rules as Seek but preserves media clock. Non-increasing ticket gives `kInvalidArgument`. |
| `Player::SetPlaybackRate` | Control; finite nonnegative multiplier (0 pauses), no borrowed data. Negative/nonfinite gives `kInvalidArgument`; unsupported positive rate gives `kUnimplemented`; platform failure is non-OK. |
| `Player::SetVolume` | Control; finite normalised linear gain in [0,1], no borrowed data. Out of range gives `kInvalidArgument`; platform failure is non-OK. |
| `Player::SetBounds` | Control; video track in punch-out mode, value bounds/z-order, nonnegative extent (zero hides). Updates may arrive every display frame, not capped at 60 Hz. Apply at next presentation and keep UI composition ordered on that sequence. Wrong track/mode/extent gives `kInvalidArgument`. |
| `Player::GetSnapshot` | Control; returns media clock, decoded/dropped/corrupted counters and audio-output latency by value. Unknown time/latency uses #51 sentinels, not guessed zero. Platform failures are non-OK. |
| `Player::GetCurrentFrame` | Control; texture video track only. Returns optional descriptor; empty means no presentable frame yet. Port-owned storage remains alive through copies, even after Stop. Punch-out/wrong track gives `kInvalidArgument`; no pixel copy. |
| `Player::Stop` | Control; common terminal barrier. Releases retained inputs/callbacks, but previously returned output owners remain valid. Idempotent success; failed quiescence is non-OK and keeps lifetime responsibilities active. |
| `Player` destruction | Owning control sequence, after successful Stop; releases exclusive instance/slot. No callback or failure return. |

## Reference-player components

The reference player owns scheduling, sync, frame drop, statistics and generation-filtered queues. Granular
decoders obey the same pull grants and ticket rules as Player, and output #51's descriptors with **port-owned**
storage. The source objects below represent the lock-free frame queue and preallocated SPSC audio ring; they do
not implement either queue. A reference player must clear or filter them on every generation change before
presentation. Direct players are allowed to return `kUnimplemented` from all component factories.

| Call | Sequence, lifetime/ownership and result |
|------|----------------------------------------|
| `ComponentFactory::CreateVideoDecoder` | Control; consumes video config, nonzero ticket and shared callbacks; returns exclusive decoder or canonical unsupported/limit/platform error. No callbacks before return or retention on failure. |
| `ComponentFactory::CreateAudioDecoder` | Control; same rules, requires audio config. Format/encryption validation precedes creation. |
| `ComponentFactory::CreateVideoSink` | Control; output mode by value; returns exclusive sink or `kUnimplemented` for unavailable mode. |
| `ComponentFactory::CreateAudioSink` | Control; returns exclusive sink or canonical platform/limit error. No source calls before Start. |
| `DecoderCallbacks::OnDecoded` | Any platform thread; transfers owned ticketed grant/output/EOS/error event to a receiver that only posts to control. Stale decoded output must be discarded there before reaching a queue. Propagate non-OK delivery; no reentry or callbacks after Stop. |
| `Decoder::WriteSamples` | Control; borrows same-track span, retains only accepted descriptors without copying compressed bytes; one grant, current ticket, valid encryption/config change. Same error/consumption rules as Player. |
| `Decoder::EndOfStream` | Control; closes input using one current-ticket grant; reports output-drained EOS asynchronously; stale is OK, duplicate/unsolicited is `kFailedPrecondition`. |
| `Decoder::Flush` | Control; strictly increasing ticket clears input/output work, grants and EOS; stale callbacks still need receiver filtering. Invalid ticket gives `kInvalidArgument`. |
| `Decoder::Stop` | Control; common terminal callback barrier, releases retained input and recipient. Existing decoded owners remain valid. |
| `Decoder` destruction | Control after successful Stop; releases exclusive decoder resources, never invalidates externally retained output storage. |
| `VideoSink::Start` | Control; retains non-null shared lock-free frame source. Null gives `kInvalidArgument`, already started/stopped gives `kFailedPrecondition`; platform failures are non-OK. |
| `VideoFrameSource::TakeDueFrame` | Platform vsync thread; `noexcept`, nonblocking lock-free pull of at most one due frame for the supplied media time. Moves descriptor ownership, never pixels; empty means no frame due. No engine/port calls, task posting or internal locks. Generation filtering belongs to the source/reference player. |
| `VideoSink::SetBounds` | Control; value bounds/z-order applied in presentation order, nonnegative extent. Same extent errors as Player; composition policy is D-19/D-23. |
| `VideoSink::Stop` | Control; common terminal barrier, including all source invocations; release source only after quiescence. |
| `AudioSink::Start` | Control; valid positive sample rate/channel count and supported sample format; retains shared preallocated SPSC source. Invalid/null gives `kInvalidArgument`, unsupported gives `kUnimplemented`, started/stopped gives `kFailedPrecondition`. |
| `AudioSource::Read` | Real-time audio thread; `noexcept`, nonblocking, allocation/lock/task-free. Borrowed writable span is a whole number of interleaved frames in Start's format; returns bytes actually read, at most span size and whole frames. Sink fills any shortfall with silence. No ownership escapes the call. Planar formats need an adapter before this sink. |
| `AudioSink::GetOutputLatency` | Control; returns current local/remote output latency by value, unknown sentinel if unavailable, non-OK on query failure. Latency is distinct from queued media duration. |
| `AudioSink::Stop` | Control; common terminal source-invocation barrier; guarantees the real-time thread can no longer read the source before releasing it. |
| Sink/source destruction | Sink on control after successful Stop. Source may be released on control only after the sink barrier; no allocations, locks or final-owner destruction on the audio callback. |

## Platform hooks

`ControlTaskRunner` is the dependency-neutral bridge, not a replacement for Umbra's scheduler. A port adapter
wraps the existing `umbra::base::SequencedTaskRunner` in platform implementation/test code, never in a Penumbra
header. All accepted tasks are deferred (never inline), sequenced/non-overlapping, ordered by due time then
posting order, with a monotonic clock. Test adapters use `TestTaskEnvironment` and its `FakeTickClock`.
Priorities and blocking policy follow ADR-0015; single-thread event loops reject `kMayBlock`.

| Call | Sequence, lifetime/ownership and result |
|------|----------------------------------------|
| `TaskRunnerProvider::CreateTaskRunner` | Control; priority and blocking policy by value; returns shared runner. Port supplies bounded workers or the required platform event loop; unsupported MayBlock gives `kUnimplemented`, allocation/budget exhaustion gives `kResourceExhausted`. Adapters share Umbra's sequence ordering and virtual clock. |
| `ControlTaskRunner::PostTask` | Any non-real-time thread; consumes move-only task even on rejection. Nonnegative finite delay required (`kInvalidArgument` otherwise). OK means accepted and deferred; stopped runner gives `kUnavailable`. Accepted tasks must run or be released during explicit runner shutdown. No silent dropping while the runner is live. |
| `ControlTaskRunner::RunsTasksInCurrentSequence` | Any thread; returns current sequence membership, not physical-thread identity; no ownership/error. |
| `LogSink::Log` | Any non-real-time thread; preserves verbose/debug/info/warning/error severity per ADR-0020/#50, borrowing an already-redacted message through return. Retain an owned string if queued; no secrets/URL query/fragment/userinfo or credential bodies (ADR-0018/ADR-0020). Return sink rejection as non-OK. Logging may not invoke port/engine callbacks inline. |
| `Port::SetMemoryPressureObserver` | Control; retains shared observer; null unregisters. On return no invocation of a displaced observer remains in flight; future callbacks use the replacement. Non-OK means registration did not change. Observer lifetime must not form a port cycle. |
| `MemoryPressureObserver::OnMemoryPressure` | Any non-real-time platform thread; value moderate/critical signal, receiver posts work to control, never re-enters or frees real-time resources inline. Non-OK is a reportable delivery failure. Unregister before releasing the owning engine. |
| `ThreadFactory::StartThread` (existing #48 hook) | Worker-creation path, never a player control/audio callback; borrows name/body at entry, implementation copies name and owns body before return. Returns exclusive joinable handle, or null for failure (existing standard-only hook, not a Status API). Task-runner adapter translates null to `kResourceExhausted`; no exceptions. |
| `ThreadHandle::Join` (existing #48 hook) | Blocking teardown path, never the joined thread/control/audio sequence; waits for owned body to finish and releases thread resource. Existing hook has no failure result; join once before handle destruction. |
| `Port` destruction | Owning control sequence after every instance is stopped/destroyed, observer is unregistered and runners are shut down on their appropriate teardown path. No callbacks or failure return. |

## Starboard lesson coverage and reserved slots

The research table predates ADR-0016. Where the accepted ADR reserves a decision, this change preserves that
reservation rather than modifying the accepted ADR body or designing the later feature.

| Research §7 row | v1 realisation or ADR-recorded deferral |
|-----------------|----------------------------------------|
| Push model / needs-data → one write | Player, NeedsData and per-stream single-use grants. |
| Seek tickets | Seek/Flush, ticketed samples/events, double generation check in CallbackDispatcher. |
| Arbitrary callback threads / no reentry | Any-thread enqueue, control-only delivery and debug CallbackScope/PortCallScope diagnostics. |
| Preferred output mode | QueryDecoding's per-stream preferred mode and StreamConfig selection. |
| Bounds and z-order | OutputBounds and SetBounds, presentation-ordered updates at display rate; D-19/D-23 reserve composition. |
| MIME/key-system capability probe | CapabilityRequest and synchronous QueryDecoding, supported/smooth/power-efficient per ADR-0016. Speed budget/cache conformance is #71. |
| In-place codec-switch query | CanChangeType and sample config_change, with recreate fallback for unsupported changes. |
| Maximum video capabilities | PlayerConfig.video_ceiling before resource allocation. |
| Capability changed/restart | PortErrorCategory::kCapability plus RecoveryHint::kRecreate and canonical non-OK Status. The current #49 catalogue has no dedicated capability-change code; D-06/catalogue follow-up assigns it without duplicating payloads here. |
| EME DRM handle/session model | ADR-0016 explicitly reserves CDM attachment to D-27; encryption metadata and StreamConfig scheme are present now, no invented CDM interface. |
| Player instance limit | GetLimits and CreatePlayer's `kResourceExhausted`; one instance per playback. |
| Local/remote audio latency | PlaybackSnapshot and AudioSink::GetOutputLatency. |
| Optional URL-player escape hatch | ADR-0016 makes it optional and delegates when/how to D-20; v1 does not invent URL, networking, DRM or reduced-feature selection APIs. |
| NPLB conformance | ADR-0016 reserves the suite; #71 implements it as the [port conformance kit](porting-guide.md). The fake port passes it, which certifies no hardware. |

**Network provider:** D-08 (#8) remains open. ADR-0016 fixes engine-owned network/demux/ABR and fragment
pass-through but no network request/response/provider shape. Therefore no network provider API is declared here;
the D-08 implementation must extend the in-tree contract under the compatibility rule above. DRM attachment,
URL-player mode and UI/ad overlay composition likewise remain with their named decisions.

**Errors:** the existing Status boundary deliberately contains no new structured payload/catalogue. #49's merged
`MakeError`/`GetErrorDetail` helpers and catalogue integrate without changing these interfaces; an integration
test passes their `kDecoderFallback` ErrorDetail through both port events and delivery failures. Capability
change/recreate and instance exhaustion still use canonical codes because the current catalogue has no dedicated
entries for them. Assigning those codes is a catalogue/D-06 follow-up, not a second payload here.

**API reference:** every call's semantics are documented here, without source comments. The installed value
types (`port_types.h`, `interface.h` and the media types) are in the
[C++ API reference](https://luisahumadamartens.github.io/alumbra/api/cpp/), whose build fails on an undocumented
or stale symbol. The abstract interfaces in `internal_include/` are not installed, so this page remains their
reference.

## Verification scope

`penumbra_port_test` implements the whole abstract contract in `test/penumbra_interface/fake_port.*`:
player/factory, both decoder paths, both sinks, capabilities, runner, logging and memory pressure. It uses
Umbra's existing manual runner and virtual clock, not sleeps or platform SDKs. Its miniature decoder emits
synthetic port-owned output, not actual codec results; it is not a codec capability oracle.

Tests exercise one-write grants, input zero-copy lifetime, fragments, queued and late stale callbacks,
cross-thread marshalling, increasing Seek/Flush tickets, EOS/reset, instance exhaustion, version rejection,
output modes, latency, canonical errors/recreate hints, Stop suppression and debug reentrancy. Queued follow-up
calls from callbacks are allowed. Decoder output owners remain alive after decoder destruction. Both installed
headers and each internal contract header compile alone at exactly C++20; the installed-consumer test additionally
rejects Abseil and C++23 in installed headers.

The fake sinks also exercise real cross-thread source calls. A lock-free atomic closed-bit/in-flight-count
barrier rejects new source entries once Stop begins. Stop returns `kUnavailable` while an earlier source call
remains in flight, without blocking or releasing the source; a later successful Stop observes its completion
and releases the source on control. The audio path neither copies shared pointers nor locks/allocates/destroys
owners. Deterministic tests suspend a source with atomics, prove Stop's nonblocking failure and retained lifetime,
resume/join it, then prove successful Stop and suppression of subsequent platform callbacks under TSan.

The fake audio sink supports **interleaved F32 only** and retains Start's channel count to calculate whole-frame
sizes. Other formats, including U8 (whose silence is 128, not zero), return `kUnimplemented` rather than falsely
claiming support. A short read keeps valid whole frames and fills the remaining F32 output bytes with zero
silence; complete underrun and callbacks after Stop likewise produce silence. A malformed source count or
non-whole-frame destination produces silence and an observable `kDataLoss` from GetOutputLatency. The fake
player emits one aggregate EOS only after all per-track EOS events in each generation, resetting on both
Seek and Flush.
