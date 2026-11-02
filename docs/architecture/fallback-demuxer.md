# Fallback demuxer slot

[ADR-0025](../adr/0025-demuxing-strategy.md) (D-05) keeps Alumbra's own parsers as the default and lets the desktop
port play the long tail of containers (AVI, FLV, Ogg and unusual MOV files) through libavformat. This page documents
the optional Penumbra slot that carries it (#322), its desktop implementation and the retry policy in Umbra. The slot
is the `kApiVersion` 3 change of the [port contract](port-contract.md#placement-and-versioning).

## The slot

The interface is in the non-installed `penumbra/interface/fallback_demuxer.h`, and `Port` implements it through
`FallbackDemuxerProvider`. Every port other than desktop, and the fake port, answers `kUnimplemented` from both calls.

| Call | Contract |
|------|----------|
| `ProbeFallbackDemuxer(window, name_hint)` | A synchronous, bounded probe over bytes that Umbra already holds. It returns `supported` and the container name, or `kUnimplemented` on a port without the slot. A window that no demuxer knows is a successful answer with `supported = false`. |
| `OpenFallbackDemuxer(request, source, callbacks)` | Returns an exclusive `FallbackDemuxer` at once and opens the container asynchronously. It retains `source` and `callbacks` until `Stop`. `kInvalidArgument` for a null recipient or a zero ticket. |

`FallbackByteSource` is what Umbra implements: `ReadAt(offset, destination)` blocks until bytes are available and
returns zero at the end of the source, and `Size` and `Seekable` describe it. The port calls it only from its own
worker, never from a control sequence, so a blocking read stalls no Umbra sequence. It never opens a file, URL or
socket itself.

`FallbackDemuxerCallbacks` follows the other callback interfaces: any thread, never under a lock, and no re-entry.

| Callback | Meaning |
|----------|---------|
| `OnOpen(info)` | The container name, one `StreamInfo` per audio or video stream with its exact time base, the duration and whether seeking works. Cover art, subtitle and data streams are left out. |
| `OnSamples(ticket, track_id, samples)` | The reply to one grant: 1 to `max_samples` samples of one track. |
| `OnEndOfStream(ticket, track_id)` | The reply to a grant when the track has no more samples. |
| `OnError(status)` | Terminal. `kInvalidArgument` for bytes that are not a container, `kUnimplemented` for no playable stream or an encrypted stream, and the source's own status when a read failed. |

`FallbackDemuxer::RequestSamples(ticket, track_id, max_samples)` grants one delivery for a track, with the same rules
as `NeedsData`: one outstanding grant per track, an older ticket is discarded with success and a newer one fails with
`kInvalidArgument`. `Seek(ticket, position)` needs a strictly larger ticket, clears every grant and queue, and
restarts at the key frame at or before the position; a source that cannot seek answers `kUnimplemented`. `Stop` is
terminal and idempotent. It returns `kUnavailable` while the worker is blocked in the source, as the common call
contract allows, and success once no callback can begin.

## Desktop implementation

`penumbra/desktop/ffmpeg_fallback_demuxer.cc` (target `penumbra::desktop_fallback`) runs libavformat on one worker
thread per demuxer.

- The bytes reach libavformat through a custom `AVIOContext` whose read and seek callbacks call `FallbackByteSource`.
  The format context uses `AVFMT_FLAG_CUSTOM_IO`, and the FFmpeg build has no protocols and no network.
- `Stop` and destruction raise an interrupt flag that the I/O callbacks and libavformat's interrupt callback check.
- Packet bytes are wrapped in `SharedBytes` through a reference to the packet's buffer, without a copy.
- A key-frame packet sets `key_frame`. New extradata becomes a `config_change`. Skip-samples side data becomes
  `trim_start` and `trim_end`. When a packet has no presentation time, the decoding time stands in for it, as AVI
  files need.
- A stream with an encrypted packet or an encryption initialisation record fails the demuxer with `kUnimplemented`,
  so `EncryptionInfo` comes only from own parsers.
- A codec that `CodecId` does not name, such as MPEG-4 Part 2, is reported with `CodecId::kUnknown`; Umbra drops such
  a track, and #437 adds codec ids later.
- Each track queues at most 1024 packets while another track is being served. A file that interleaves further apart
  fails with `kResourceExhausted`.

`components.txt` enables the demuxers `mov`, `matroska`, `ogg`, `avi`, `flv`, `mpegts`, `flac`, `mp3`, `ac3`, `eac3`,
`aac`, `ivf`, `h264` and `hevc`. `mov`, `matroska` and `mpegts` cover the own-parser containers, so that the retry has a
second parser. WAV is not enabled because the build has no PCM decoder.

## Selection and retry in Umbra

`umbra/demux/fallback_policy.h` decides which parser takes a file. It holds no FFmpeg code, only the
`demux.ffmpeg.mode` value and whether the port has the slot. A port without the slot behaves as `off`.

| Mode | First parser | After a rejection |
|------|--------------|-------------------|
| `fallback` (default) | The own parser, or the slot for a container with none. | A rejection before the first sample retries once with the slot. |
| `prefer` | The slot for progressive sources when it accepts the file. | The own parser takes over when the slot rejects the file. |
| `off` | The own parser. | None. |

A rejection after the first sample never switches parsers. A retry is logged in the `demux` category at warning level
with the container and the own parser's status, and `FallbackStats::retries` counts it. The hook belongs where the
progressive demuxer registry of container probing (#76) reports that no own parser takes the source: it asks the
policy with `ChooseFirst` and, after an own parser fails, `ChooseAfterOwnRejection`. Exposing the counter in the
debug statistics extension of ADR-0020 is part of that wiring.

## Tests

| Test | Covers |
|------|--------|
| `penumbra_port_test` (`FallbackDemuxerContractTest`) | The fake port answers `kUnimplemented` to both calls and retains no recipient. |
| `umbra_demux_unittests` (`FallbackPolicyTest`) | Every mode, a port without the slot, the retry rules, the log record and the counter, with the MP4 demuxer's rejection of a file that has no playable track. |
| `ffmpeg_fallback_demuxer_test` | Probing, AVI, Ogg Opus, FLV and MP4 playback through the slot with partial reads, grants, seeks, a source that cannot seek, `Stop` with a blocked source, a source failure, an encrypted file, and an MP4 that the own parser rejects and the slot plays. |
| `ffmpeg_components_test` | The demuxer list of `components.txt` against the built libraries. |
| `layering_include_test` and `layering_target_graph_test` fixtures | libavformat headers and libraries are rejected in Umbra and accepted in a port. |

The clips are in `test/fixtures/fallback_demux/`, made by `generate.sh` with FFmpeg `lavfi` sources and kept to a
few kilobytes each. Nothing in the tests uses the network or sleeps.

## Follow-ups

- Umbra's `FallbackByteSource` over the data source, with the cancellation that lets `Stop` finish.
- The hook in the progressive demuxer registry of #76, and the statistics counter.
- A libFuzzer target that drives the enabled demuxers through the bridge (ADR-0025).
- Conformance cases for both answers of the slot in the kit of #71.
