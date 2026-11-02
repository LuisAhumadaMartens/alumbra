# Decoder chain

[ADR-0029](../adr/0029-decoder-selection-and-fallback.md) (D-06) decides how the reference player chooses a decoder
and when it switches. This page documents the implementation of #84 in `penumbra/reference_player/` (target
`penumbra::reference_player`, non-installed). Code comments are not used in Alumbra, so the rules live here.

The module is shared by every port built from components. It depends only on the Penumbra interface and Abseil, and
Umbra never includes it: Umbra sees capabilities, the decoder settings it passes in `PlayerConfig.decoder`, and
errors with a recreate hint.

## Contract changes (`kApiVersion` 2)

`ComponentFactory` exposes the candidate list. `CreateVideoDecoder` and `CreateAudioDecoder` became
`GetDecoderCandidates(StreamType)` and `CreateDecoder(candidate, config, ticket, callbacks)`, a source-incompatible
change under the [versioning rule](port-contract.md#placement-and-versioning), so `kApiVersion` is now 2.
`PlayerConfig.decoder` is an additive value field holding the resolved decoder settings.

| Type (header `decoder_types.h`, installed, C++20 only) | Meaning |
|------|---------|
| `DecoderKind` | `kHardware`, `kPlatformSoftware` or `kBundledSoftware`: the three ranks of the order |
| `DecoderOutput` | `kCpuBuffer` or `kGpuSurface` (`PixelFormat::kOpaque` frames, [media types](media-types.md#decoded-output)) |
| `DecoderCandidate` | Name, kind, output, `secure`, supported codecs, optional maximum size (zero means unlimited) and port-defined quirk tags |
| `HardwarePreference` | `decode.prefer_hardware`: `kAuto`, `kAlways` or `kNever` |
| `DecoderSettings` | `prefer_hardware`, `fallback_enabled`, `dropped_frames_max_percent`, `dropped_frames_window_ms`, `preferred_decoders`, `blocklist_extra_decoders`, `blocklist_ignored_entries` |

The settings keys themselves (`decode.*`) are added to the schema by #327 and resolved by Umbra's settings runtime
(#54). Until then callers fill `DecoderSettings` directly; the defaults equal the keys' defaults.

## Selection

`SelectDecoderCandidates` turns a port's candidate list into the chain's ordered list:

1. Drop candidates that do not support the stream's codec or size, and, for a secure stream (`require_secure`),
   every non-secure candidate.
2. Apply `prefer_hardware`: `never` drops hardware candidates, `always` drops the others. A secure candidate that
   the stream requires is never dropped by this setting.
3. Drop candidates that the blocklist matches (an entry listed in `blocklist_ignored_entries` is not applied) and
   candidates named in `blocklist_extra_decoders`. An item is `name` or `name:codec`, where `codec` is one of the
   lowercase codec names (`h264`, `hevc`, `vp9`, `av1`, `aac`, ...).
4. Stable-sort by kind: hardware, then platform software, then bundled software. Within a kind the port's order
   stands.
5. Move the names in `preferred_decoders` to the front in the given order. A name that step 1 to 3 removed is not
   restored.

`DecoderChain::Create` first asks `CapabilityProvider::QueryDecoding` with the caller's `CapabilityRequest`. An
unsupported answer, or an empty list after selection, fails with `kUnimplemented` (catalogue code
`CodecUnsupported`, 4001).

## The chain

`DecoderChain` implements `Decoder`, so the reference player drives one decoder per stream and the chain hides
the candidates behind it. Everything runs on the control sequence. Decoders may call `DecoderCallbacks` on any
thread: the chain's per-candidate callbacks only post a task to the `ControlTaskRunner`, and the task drops events
of a retired decoder instance and events whose ticket differs from the current one. A flush therefore never
delivers a stale frame, including frames already queued when it ran.

**Grants.** The chain forwards a decoder's `NeedsData` grant to its owner once, and only when it has nothing queued.
It checks the owner's writes itself (current ticket, one write per grant, at most `max_samples`, one track) and
queues the accepted samples. A write goes to the decoder only while the decoder holds a grant, split into several
writes when the new decoder's grant is smaller. The queue is therefore bounded by the largest grant the chain gave
out. `EndOfStream` is sent to the decoder after the queued samples.

**When the chain moves on** (always forward; a candidate it left is excluded for the rest of the playback):

| Situation | Behaviour |
|-----------|-----------|
| A candidate fails in `CreateDecoder` | Next candidate at once, one `DecoderFallbackEvent` per move |
| An error, or a synchronous write failure, before the first output | Next candidate; the retained samples are re-fed |
| A configuration change the decoder rejects with `kUnimplemented` after it produced output | The same candidate is recreated with the new configuration; if that fails to create, the chain moves on |
| An error after the first output, other than a corrupted sample | `PortError` with `kRecreate` goes to the owner. The next `Flush` (the reseek, with a new ticket) recreates the same candidate. If the recreated decoder fails before its own first output, the chain moves on |
| `SwitchToNextCandidate()` (the drop trigger, M2) | The next `Flush` moves to the next candidate with reason `kDroppedFrames`; the call fails with `kFailedPrecondition` when no candidate remains or fallback is disabled |

A corrupted sample after the first output (`kDataLoss` with a `kTransient` hint) is counted in
`DecoderChainStats::corrupted` and never causes a fallback.

When no candidate remains, `Create` returns, and a running chain reports as `PortError` with `kFatal`: `kInternal`
(`DecoderInitializationFailed`, 4003) if a candidate ran or failed to create, or `kUnimplemented`
(`CodecUnsupported`, 4001) if every candidate refused the configuration. Umbra maps these to the catalogue. Every
move is reported to `DecoderChainObserver::OnFallback` with the previous and the next decoder, the reason
(`FallbackReason`) and the cause. Umbra turns it into `DecoderFallback` (4002, degraded) and then asks for
capabilities again.

With `fallback_enabled` false the first failure is reported as an error (at creation, or as `kFatal`) and the chain
never moves on. Tests that compare golden hashes use this together with `prefer_hardware` set to `never`.

Secure streams: `require_secure` removes every non-secure candidate before the chain starts, so no fallback path
reaches a non-secure decoder.

## Retained samples

From the moment a decoder instance starts (creation or a flush) until its first output, the chain keeps the samples
it accepted. They are `EncodedSample` descriptors that share the bytes, so nothing is copied. The first output
releases them. A fallback before then queues them again for the new decoder. `DecoderChainConfig::max_retained_samples`
(default 512) bounds the memory: beyond it the chain stops retaining, and a failure before the first output is then
reported as `kRecreate` and handled with a reseek and the next candidate at the following flush. The retained
count is in the statistics and belongs in D-12's budgets.

The configuration in force when retention started is used for a fallback, so a configuration change inside the
retained samples is applied again by the new decoder.

Limitation: when a decoder is recreated for a configuration change, frames still inside the old decoder are lost,
because the grant model has no drain step. Decoders that accept the change in place (the desktop decoders of #85
and #86 should) do not lose frames.

## Drop attribution and statistics

`DropAttribution` records dropped frames by cause and evaluates the trigger of ADR-0029 over tumbling windows. The
reference player calls `RecordFrameDue` for every frame due (including dropped ones) and `RecordDroppedFrame` with
`DropCause::kDecodeLate` or `kPresentLate`, passing a monotonic `TimeDelta`. When a window of
`dropped_frames_window_ms` ends with more than `dropped_frames_max_percent` of the frames due decode-late dropped,
`DecoderChainObserver::OnDropTrigger` receives a `DropTriggerEvent` and the next window starts. Present-late drops
never count. The window restarts on `Flush` and can be restarted by the owner on resume, rate change and rendition
change. `UpdateDropThresholds` applies the live settings.

In M1 the event is recorded and nothing acts on it. In M2 Umbra answers it by capping the resolution through ABR
and, at the lowest rung, calling `SwitchToNextCandidate()` and flushing under a new ticket (in place), or by
recreating the port player with the slow decoder in `blocklist_extra_decoders`.

`DecoderChain::GetStats` returns the decoder name, kind and output, and counters for decoded, corrupted,
decode-late and present-late dropped frames, fallbacks, recreations, drop triggers and retained samples. The public
dropped-frames statistic is the sum; the split belongs to the debug extension.

## Blocklist

The blocklist is the reviewed file [`decoders/blocklist.toml`](../../decoders/blocklist.toml). `tools/decoders/generate_blocklist.py`
validates it and writes the C++ table `penumbra/reference_player/include/penumbra/reference_player/decoder_blocklist_table.h`
and the [reference page](../reference/decoder-blocklist.md). CTest runs `decoder_blocklist_drift`
(`generate_blocklist.py --check`) and `decoder_blocklist_generator`. The generated files are committed.

```toml
version = 1

[[entry]]
id = "vendor-h264-green-frames"
decoder = "vendor.h264.decoder"
codec = "h264"
port = "android"
os_min = "12"
os_max = "13"
device_model = "Model X"
gpu_vendor = "acme"
gpu_device = "1234"
driver_min = "1.2"
driver_max = "1.4.9"
reason = "Green frames after a seek"
evidence = "https://github.com/LuisAhumadaMartens/alumbra/issues/1"
since = "0.1.0"
```

`id`, `decoder`, `reason`, `evidence` (an https link) and `since` are required. The other fields narrow the match
and an empty field matches everything: `codec` (a lowercase codec name), `port`, `os_min` and `os_max`,
`device_model`, `gpu_vendor`, `gpu_device`, `driver_min` and `driver_max`. Versions are dotted digits and ranges are
inclusive. IDs are unique kebab-case. An entry whose range or text field the device profile cannot confirm (an empty
or malformed value) does not match, so an unknown device keeps its decoders. The file is empty today: no decoder is
known to be bad yet. Integrators exclude more decoders with `blocklist_extra_decoders` and ignore built-in entries
with `blocklist_ignored_entries`; Alumbra fetches nothing.

The port supplies the `DeviceProfile` (port, OS version, device model, GPU vendor and device, driver version) in
`DecoderChainConfig`. This change delivers most of #334: the TOML format, the generator with its drift check, the generated reference page
and matching in the decoder chain. What remains of #334 is the entries that switch off a non-decoder feature, such as
display rate matching for known-bad variable-refresh-rate drivers (used by #68), and exposing
`decode.blocklist.extra_decoders` and `decode.blocklist.ignored_entries` as settings keys, which #327 adds.

## Verification

`penumbra_reference_player_test` covers selection, the blocklist, drop attribution and every trigger of the chain
with a scripted fake decoder (`test/penumbra_reference_player/scripted_decoder.*`) and the fake port: a creation
failure and the error only when all candidates fail, early errors with re-fed samples, mid-stream errors with a
reseek and recreation, exclusion for the rest of the playback, resolution changes in place and by recreation, flush
without stale frames, grant enforcement, secure streams, `preferred_decoders`, retention bounds, drop statistics and
the trigger, and Stop. The fake port implements the new factory methods. Real decoders arrive with #85 and #86.
