# 0025 — Parse core containers in Umbra and use libavformat only as a desktop fallback

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #5

## Context

D-05 decides who parses media containers: Alumbra's own incremental C++ parsers, FFmpeg's libavformat, or a
defined split between them. The parser decides what the engine can see: CENC metadata (`tenc`, `senc`,
`saiz`/`saio`, `pssh`) for D-27 (#27), `emsg` and ID3 timed metadata for D-26 (#26), `prft` and `sidx` boxes,
CEA-608/708 captions carried in H.264 and HEVC SEI (#136), exact timestamps, and the codec configuration behind
RFC 6381 codec strings (#81). Container parsers are also the largest attack surface for untrusted input: Android's
2015 Stagefright vulnerabilities were in its MP4 parser.

Alumbra needs ISO-BMFF (progressive MP4 and fMP4/CMAF), MPEG-TS and HLS packed audio, WebM/Matroska, and a
VLC-like long tail on desktop: AVI, FLV, Ogg, WAV, FLAC and unusual MOV files. D-05 blocks probing (#76), the MP4,
TS and WebM demuxers (#78, #79, #80) and codec strings (#81). M1's exit containers (progressive MP4, MPEG-TS and
WebM/MKV) are all parsed by Alumbra's own code under this decision, so libavformat is not on M1's critical path.
Where MPEG-TS is remuxed to fMP4 for MSE remains D-16's (#16) decision.

Prior art splits the same way. Chromium has its own stream parsers for MSE in `media/formats` and uses
`FFmpegDemuxer` for `src=` playback. Media3 uses only its own Java extractors and uses FFmpeg for decoding alone.
VLC and GStreamer have their own demuxers, with an avformat fallback module or wrapper. mpv uses libavformat plus
its own Matroska demuxer. hls.js and Shaka Player demux MPEG-TS in JavaScript.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Licensing and platforms** ([ADR-0014](0014-dependency-and-licensing-policy.md#linkage-and-distribution)).
  LGPL code, and so libavformat, is barred from every iOS, iPadOS, tvOS and web artefact. No Android LGPL
  dependency is planned, and any future one needs its own review. On desktop, FFmpeg ships only as separately
  replaceable shared libraries, built with `--disable-network` and a reviewed component enable-list, and it reads
  bytes only through a custom `AVIOContext`. This answers the issue's question about Android and iOS. The issue
  cites ADR-0011 for the linking rule; the rule is ADR-0010's, made precise by ADR-0014.
- **Fuzzing** ([ADR-0021](0021-testing-strategy.md#fuzzing-sanitizers-and-failure-handling)). Every parser of
  untrusted input gets a registered libFuzzer target. #45 owns the harness and bounded smoke runs, #267 owns
  corpus growth, and Alumbra applies to OSS-Fuzz only once it is eligible. This closes the issue's question about
  fuzz corpora and OSS-Fuzz onboarding.
- **The sample type** (#51, [media types](../architecture/media-types.md)). `EncodedSample` carries exact
  `Timestamp`s, key-frame and discontinuity flags, configuration changes, `trim_start`/`trim_end`, HDR dynamic
  metadata and `EncryptionInfo`. This closes the issue's question about a sample type shared with D-04.
- **Threading** ([ADR-0015](0015-threading-and-task-runners.md#blocking-work)). Nothing blocks a sequence, and the
  single-threaded web build needs long work to be incremental, so parsers must be incremental and non-blocking.

The issue's recommendation conflicts with accepted decisions in two places, which this ADR resolves:

- It places libavformat "behind Umbra's `Demuxer` interface". The
  [layering allowlist](../architecture/layering.md#the-allowlist) does not allow that: an Umbra target may link
  only Umbra, the Penumbra interface, Abseil and `Threads::Threads`, and the check fails closed. The fallback's
  placement below resolves this.
- It proposes one sample type "carrying CENC subsamples and side data". Any field added to `EncodedSample`
  changes the port contract and increments `penumbra::kApiVersion`
  ([port contract](../architecture/port-contract.md#placement-and-versioning)). Timed metadata therefore leaves
  the demuxer as separate streams instead.

## Decision drivers

- Full control of CENC, `emsg`, `prft`, SEI captions and exact timestamps on the streaming path, on every platform.
- Small WASM and mobile builds, with no LGPL code where ADR-0014 bars it.
- VLC-like breadth on desktop for files the core parsers do not cover.
- Incremental, non-blocking parsing that works in the single-threaded web build and opens files with the index at
  the end without downloading them first.
- A strict layering check, and a port contract that changes only where it must.
- An attack surface that is bounded and fuzzed in-house.

## Considered options

The definition has seven questions.

Strategy:

1. **A — Hybrid:** own parsers for the core containers on every platform; libavformat for the long tail on
   desktop only.
2. **B — Own parsers only:** no libavformat; the long tail arrives as own parsers over time, or never.
3. **C — libavformat first, own parsers later:** desktop M1 uses libavformat for everything.

Where the libavformat fallback lives:

1. **A — Desktop port, optional Penumbra slot.**
2. **B — Umbra module with a layering-allowlist exception**, linked only into desktop builds.
3. **C — Defer until after M1.**

The v1 own-parser set:

1. **A — Core set plus ADTS, MP3 and ID3,** with defined Matroska coverage.
2. **B — Exit-criteria containers only** (MP4, TS and WebM/MKV), with ADTS and MP3 in M2.
3. **C — Core set plus Ogg, FLAC and WAV.**

The parser API:

1. **A — Push parser with seek requests,** as in Media3's extractors.
2. **B — Pure push stream** with no repositioning.
3. **C — Random-access reader** with a blocking `Read(offset, size)`.

SEI captions, `emsg` and ID3:

1. **A — Separate timed streams,** with `EncodedSample` unchanged.
2. **B — Generic side data on samples.**

Timestamp normalisation:

1. **A — Single edit, unwrap, flag jumps.**
2. **B — Full edit-list support.**
3. **C — Ignore edit lists.**

Rejections and platform scope:

1. **A — Automatic fallback, desktop only.**
2. **B — Fall back only when an own parser reports an unsupported feature.**
3. **C — Never automatic;** libavformat only when an integrator prefers it.

## Decision

Chosen options: **A for all seven questions.** Alumbra needs its own parsers on the web, Android and Apple
platforms anyway, and they give full control on the streaming path. libavformat adds breadth on desktop without
entering Umbra, so the layering check stays strict. The split follows Chromium's structure, and the parser API
follows Media3's extractors.

### Own parsers and their coverage

- Umbra's own incremental C++ parsers handle these containers on every platform:
  - ISO-BMFF: progressive MP4 and fragmented MP4/CMAF;
  - MPEG-TS;
  - WebM/Matroska;
  - ADTS and MP3 elementary streams with their ID3 tags, including HLS packed audio.
- Matroska coverage is SimpleBlock and BlockGroup; Xiph, EBML and fixed lacing; Cues seeking, with a bounded
  cluster scan when Cues are missing; CodecPrivate, CodecDelay and SeekPreRoll; ContentEncoding for WebM
  encryption and header stripping; and attachments, which supply fonts to M3's text rendering (#140).
- Ordered chapters and linked segments are not supported. Such a file plays its physical segment in storage order,
  and the omission is logged. ContentEncoding compression other than header stripping is not supported in v1: a
  track that uses it is not offered, and a file left with no playable audio or video track is rejected.
- On desktop, libavformat handles containers with no own parser (AVI, FLV, Ogg, WAV, FLAC and unusual MOV files)
  and files that an own parser rejects, as described under [rejections](#rejections-and-platform-scope).
- Adaptive streaming never uses libavformat. HLS and DASH segments (MPEG-TS, fMP4/CMAF, WebM and packed audio)
  always go through the own parsers, because only they surface CENC, `emsg` and `prft` and allow fragment
  pass-through. HLS packed AC-3 and E-AC-3 (#108) reuse the MPEG-TS demuxer's sync-frame framing (#79).
- Container detection is #76's content sniffing over a bounded probe window. Content type and file extension only
  break ties.

### libavformat lives in the desktop port

- The Penumbra interface gains an optional **fallback demuxer** slot. The desktop port implements it with
  libavformat, next to #85's FFmpeg decoders and its licence test. Every other port answers `kUnimplemented`, the
  port contract's code for an optional unsupported path.
- The slot offers a bounded probe over bytes Umbra already holds, then an open over a byte source that Umbra
  implements. The port wraps that byte source in a custom `AVIOContext`. FFmpeg's read and seek callbacks become
  byte-range requests to Umbra's data source, so FFmpeg never opens a file, URL or socket itself (ADR-0014).
- libavformat's read model blocks, so the port runs it on its own `MayBlock` worker, which waits for Umbra's bytes.
  No Umbra sequence ever blocks.
- The slot's output follows the existing contract rules: stream descriptions as `StreamInfo` with each stream's
  exact time base, then `EncodedSample`s under one-write pull grants, seeks that carry a ticket, callbacks posted
  to the control sequence, and no callbacks after Stop. Packet bytes are wrapped in `SharedBytes` without a copy.
  Key-frame flags, new extradata and FFmpeg's skip-samples side data map to the sample's flags, configuration
  change and `trim_start`/`trim_end`.
- The slot never carries encryption. The port rejects a stream that libavformat reports as encrypted, so
  `EncryptionInfo` comes only from own parsers and D-27 has one metadata path.
- Samples from the slot re-enter Umbra like samples from an own parser. Track selection, timestamp checks,
  discontinuity handling, codec strings (#81) and SEI caption extraction, which works on samples rather than
  containers, all stay in Umbra.
- This is a narrow exception to [ADR-0002](0002-portable-engine-porting-layer-html5-api.md) and
  [ADR-0016](0016-penumbra-port-contract.md), which give demuxing to Umbra. Umbra still owns probing, the choice
  of parser, the data source and everything downstream of the samples; the port only turns long-tail container
  bytes into samples. ADR-0016's body does not change: the slot is one more optional part of its contract.
- The layering allowlist does not change. FFmpeg headers and libraries appear only in `penumbra/desktop`, which may
  already link its platform libraries.
- The slot is a source-incompatible addition to the in-tree contract, so it increments `penumbra::kApiVersion`,
  currently 1, under the port contract's versioning rule. Its exact names and headers belong to the port-contract
  change listed under follow-ups.

### Push parsers with seek requests

- Parsers are push parsers and never block. Umbra appends bytes at known byte offsets, and each parse step returns
  one of:
  - **need more data**;
  - **continue at byte offset N**, which the data source turns into a range request;
  - **yield**, when the step has used its work budget;
  - **end of stream**, or an error `absl::Status` in the [error catalogue](../architecture/errors.md)'s 3xxx
    demux and media range.
- Parsers emit tracks, samples, timed metadata and a seek map through an output interface. A sample that lies
  inside one appended buffer is a `SharedBytes` slice of it, so its bytes reach the port without a copy. A sample
  that spans buffers is assembled with one copy.
- Seeking uses the seek map: MP4 sample tables and `sidx`, Matroska Cues, Xing or VBRI tables in MP3, and
  timestamp bisection over byte ranges in MPEG-TS (#79). A seek to a media time becomes a byte offset, the parser
  resets, and the data source continues from that offset.
- A progressive MP4 with `moov` after `mdat` opens with a few range requests: the parser skips `mdat` by its size,
  reads `moov`, then continues at the first sample. A Matroska file with Cues at the end works the same way through
  its SeekHead.
- On a source without range support, a forward request is served by discarding bytes. Whether a backward request
  reopens the resource or fails the open is the data source's decision (#73, #76).
- Each step is bounded, so the single-threaded web build yields within ADR-0015's time budget
  (`engine.threads.yield_budget_ms`). Untrusted sizes are read through #45's bounds-checked byte and bit readers,
  nesting depth is limited, and no allocation grows with an untrusted size field without a cap (#82).

### Captions, `emsg` and ID3 leave as timed streams

- `EncodedSample` does not change, so this part of the decision needs no port-contract change.
- Umbra extracts timed metadata into separate streams. Each item carries a `Timestamp`, an optional duration and
  its payload as `SharedBytes`:
  - CEA-608/708 `cc_data` from H.264 and HEVC `user_data_registered_itu_t_t35` SEI messages (ATSC A/53 `GA94`),
    for the text pipeline (D-15, #15, and #136);
  - `emsg` boxes of versions 0 and 1, keeping the scheme, value, timescale, times, ID and message data, for D-26
    and D-15;
  - ID3 frames from MPEG-TS timed-metadata streams (stream type `0x15`), from packed audio, and from `emsg` boxes
    that use the AOM ID3 scheme.
- Caption extraction scans the NAL units of every H.264 and HEVC sample, including samples from the desktop
  fallback. It copies `cc_data` out and leaves the sample bytes unchanged. #136 reorders the data from decode
  order to presentation order.
- In packed audio, the ID3 `PRIV` frame `com.apple.streaming.transportStreamTimestamp` also sets the time of the
  segment's first frame (#108).
- `prft` boxes feed live-latency measurement ([ADR-0020](0020-observability-logging-tracing-qoe-metrics.md)),
  `sidx` feeds the seek map, and CENC boxes become `EncryptionInfo` and protection-system data for D-27 (#236).
- Timed metadata never reaches the port.

### Timestamps are normalised in the parsers

- **Edit lists.** One edit is applied. A leading empty edit becomes a delay, and the first non-empty entry's
  `media_time` becomes the start offset. For audio, the samples that straddle the start and end of the edit stay
  in the stream and carry the amount to discard in `trim_start` and `trim_end`, so decoders prime correctly and
  gapless playback works. Further entries are ignored, and that is logged. The offset is applied in the track's
  own time base with integer arithmetic; where the movie and media timescales differ, the conversion uses
  `Rescale` with an explicit rounding mode.
- **Composition offsets.** Signed `ctts` offsets (version 1) are supported, so a presentation timestamp may precede
  its decode timestamp. Values are carried exactly and never clamped.
- **MPEG-TS wraparound.** Each 33-bit PTS and DTS is unwrapped into 64 bits relative to the previous value in the
  same program, choosing the nearest candidate. The timeline stays continuous across the rollover, which comes
  about every 26.5 hours on the 90 kHz clock.
- **Discontinuities.** A jump between consecutive unwrapped timestamps beyond a threshold, or a set
  `discontinuity_indicator`, sets the sample's discontinuity flag. #79 chooses, documents and tests the threshold;
  it is a constant, not a setting. HLS `EXT-X-DISCONTINUITY` tags remain the HLS layer's to handle (#110).
- **Other priming.** Matroska CodecDelay and DiscardPadding, and the encoder delay and padding in an MP3's LAME
  header, also map to `trim_start` and `trim_end`.

### Rejections and platform scope

`demux.ffmpeg.mode` selects the behaviour on ports that implement the fallback slot:

| Mode | Behaviour |
|------|-----------|
| `fallback` (default) | Own parsers first. libavformat opens containers with no own parser, and retries a file that an own parser rejects before its first sample. |
| `prefer` | libavformat first for progressive sources. An own parser takes over when the slot rejects the file or a stream is encrypted. |
| `off` | libavformat is never used. A container without an own parser fails as unsupported. |

- A rejection is an error from the own parser before it has emitted any sample of the file. After the first
  sample the parser never switches: corrupt data later in the file is handled by the own parser's
  resynchronisation or fails with a 3xxx code, because a switch would change tracks and timestamps during
  playback.
- A retry re-reads the file from its start. It is logged in the `demux` category at warning level, with the own
  parser's status and the probed container, and counted in the unstable debug extension of ADR-0020's
  statistics. Under [ADR-0018](0018-error-model-and-recovery.md) it is a recoverable condition: it goes to
  telemetry, not to the app as a warning event.
- When no parser accepts a source, playback fails with a 3xxx demux code whose `MediaError` code is
  `MEDIA_ERR_SRC_NOT_SUPPORTED`. The error catalogue names the code itself.
- libavformat ships on desktop only: Linux, macOS and Windows. It never ships on Android, Google TV, iOS, iPadOS,
  tvOS or the web. On those ports every mode behaves as `off`, and the setting's platform metadata reports it as
  unsupported there ([ADR-0017](0017-settings-schema-and-configuration.md#platform-availability)). Shipping
  libavformat on Android would need a new ADR and ADR-0014's review of that dependency.

### Settings

New keys follow [ADR-0017](0017-settings-schema-and-configuration.md#naming) and enter the schema through the tasks
that implement them, not through this ADR:

- `demux.ffmpeg.mode`: enum `fallback`, `prefer` or `off`; default `fallback`; platforms `linux`, `macos` and
  `windows`; integrator audience; `experimental`; applied at `next_load`, so a per-source override can select it.
  The key names a library, not a platform, so it meets the naming rule.
- #78 proposes `demux.mp4.ignore_edit_lists`, a negative boolean that ADR-0017 does not allow. If #78 keeps that
  escape hatch, it enters the schema as `demux.mp4.edit_lists.enabled`, default `true`, applied at `next_load`.
- #76's `source.probe.max_bytes` and #79's `demux.ts.program_number` already follow the naming rules.

## Consequences

**Positive**
- The streaming path has full control of CENC, `emsg`, `prft`, SEI captions and exact timestamps on every
  platform.
- WASM and mobile builds carry only the core parsers and no LGPL code.
- Desktop plays the long tail and many broken but common files, as VLC does.
- The layering check stays strict, and FFmpeg stays in one layer next to the decoders that already use it.
- Files with the index at the end open with a few range requests, and parsing works single-threaded on the web.
- `EncodedSample` is unchanged, and the text and ad code receive clean timed streams.

**Negative / costs**
- Desktop has two parsing paths to test, and the port contract grows an optional slot with a `kApiVersion` bump.
- Push parsers that issue seek requests are state machines, which are harder to write and review than readers
  that pull bytes.
- The caption scan reads the NAL units of every H.264 and HEVC sample.
- Malformed files can reach a second parser on desktop. libavformat is fuzzed upstream on OSS-Fuzz, but it is
  memory-unsafe code that processes untrusted input inside Alumbra's process. ADR-0014's narrowed build limits the
  enabled demuxers; process isolation is not part of this decision.
- Multi-entry edit lists are approximated, and Matroska ordered chapters and linked segments are not supported.
- AVI, FLV, Ogg, WAV and FLAC files do not play on Android, Apple mobile platforms or the web in v1. An own parser
  for any of them needs a new task.
- Long-tail containers bring long-tail codecs. A file that the fallback demuxes still needs a decoder, so desktop
  breadth also depends on #85's decoder set.

**Follow-ups**
- **Port-contract change** (new task, after #67's v1 headers): add the optional fallback-demuxer slot to the
  internal Penumbra headers, document it in [port-contract.md](../architecture/port-contract.md) and the
  [overview](../architecture/overview.md)'s port table, increment `penumbra::kApiVersion`, make the fake port
  answer `kUnimplemented`, and add conformance cases to #71 for both answers.
- **Desktop fallback demuxer** (new task): libavformat behind the slot in `penumbra/desktop`, with the
  `AVIOContext` bridge on a `MayBlock` worker, the packet-to-sample mapping and the rejection of encrypted streams.
  Its reviewed demuxer enable-list, required by ADR-0014 and recorded through #40, covers the long tail and
  libavformat's demuxers for the own-parser containers, which the automatic retry needs. A libFuzzer target drives
  the enabled demuxers through the bridge.
- **ADTS, MP3 and ID3 parsers** (new task under #77): the elementary-stream parsers, Xing and VBRI seeking, LAME
  priming and ID3 extraction.
- **#76:** sniffing for the own-parser set, the slot's probe for everything else, `demux.ffmpeg.mode` and its
  schema record, the retry before the first sample, and its log entry and diagnostic counter.
- **#78:** the single-edit rule, signed `ctts`, a trailing `moov` reached through seek requests, `emsg`, `prft`
  and `sidx`, and the positive edit-list key if the escape hatch stays.
- **#79:** the 33-bit unwrap, the discontinuity threshold, bisection seeking through seek requests, and stream
  type `0x15` as a timed-metadata stream.
- **#80:** the Matroska coverage above, including attachments and WebM encryption metadata, without ordered
  chapters or linked segments.
- **#81:** codec strings for streams from the own parsers and from the fallback slot, built from the
  configuration records and extradata they return.
- **#82:** fuzz targets for every own parser, adding the ADTS, MP3 and ID3 parsers and `emsg` to its list, on #45's
  harness with #267's corpus growth.
- **#85:** shares its narrowed FFmpeg build, configuration assertions and licence test with the fallback slot, and
  its decoder list states which long-tail codecs desktop covers.
- #108 reuses #79's sync-frame framing for packed AC-3 and E-AC-3. #136 with D-15 (#15), and D-26 (#26), consume
  the timed streams. D-27 (#27) and #236 consume the CENC metadata.

## Pros and cons of the options

### Strategy

- **A — Hybrid (chosen).** Good, because it gives full control on the streaming path, small WASM and mobile
  builds, and VLC-like breadth on desktop. Bad, because desktop has two parsing paths to test.
- **B — Own parsers only.** Good, because there is one path, all in C++ and fuzzed in-house. Bad, because the
  desktop "plays anything" goal weakens, and AVI, FLV and Ogg cost Alumbra's time.
- **C — libavformat first.** Good, because it is the quickest route to First Light. Bad, because ADR-0014 bars it
  from the web and Apple mobile platforms, so own parsers are needed anyway, and FFmpeg's side-data gaps and
  blocking I/O model would be baked into M1.

### Where the fallback lives

- **A — Desktop port, optional slot (chosen).** Good, because FFmpeg stays in `penumbra/desktop` with #85's
  decoders and licence test, and the layering rules are untouched. Bad, because it extends the port contract and
  makes a narrow exception to "Umbra owns demuxing".
- **B — Umbra module with an allowlist exception.** Good, because every demuxer sits in one layer. Bad, because it
  puts the first third-party media library inside Umbra and adds a platform-conditional exception to a check that
  otherwise fails closed.
- **C — Defer.** Good, because it costs nothing in M1. Bad, because it only postpones the decision.

### v1 own-parser set

- **A — Core set plus ADTS, MP3 and ID3 (chosen).** Good, because it covers M1 and M2's HLS packed audio. Bad,
  because ADTS and MP3 arrive slightly before they are strictly needed.
- **B — Exit-criteria containers only.** Good, because M1 is smallest. Bad, because raw `.mp3` files would not play
  in M1 although MP3 decoding is an exit criterion.
- **C — Add Ogg, FLAC and WAV.** Good, because it adds coverage without FFmpeg. Bad, because libavformat already
  covers them on desktop.

### Parser API

- **A — Push parser with seek requests (chosen).** Good, because it works single-threaded on the web and opens
  files with the index at the end efficiently. Bad, because parsers become state machines.
- **B — Pure push stream.** Good, because it is the simplest. Bad, because a file with `moov` at the end must
  download completely before playback starts.
- **C — Random-access reader.** Good, because parser code is the easiest to write. Bad, because a blocking read
  blocks a sequence, which ADR-0015 forbids and the web build cannot do.

### Captions, `emsg` and ID3

- **A — Separate timed streams (chosen).** Good, because the port contract stays unchanged and consumers get clean
  cues. Bad, because the demuxer must scan NAL units for SEI.
- **B — Generic side data on samples.** Good, because it is flexible. Bad, because it bumps the port contract's
  version, and every port must ignore the list safely.

### Timestamps

- **A — Single edit, unwrap, flag jumps (chosen).** Good, because it covers the cases Chromium and Media3 handle,
  including AAC priming, and gapless audio works. Bad, because rare multi-entry edit lists are approximated.
- **B — Full edit-list support.** Good, because edited files play exactly. Bad, because it is complex for little
  real-world gain.
- **C — Ignore edit lists.** Good, because it is the simplest. Bad, because common MP4 files get AAC priming errors
  and audio and video misalignment.

### Rejections and platforms

- **A — Automatic fallback, desktop only (chosen).** Good, because desktop gets VLC's "it just plays" while other
  platforms are untouched. Bad, because malformed files reach a second parser.
- **B — Fall back on "unsupported" only.** Good, because it is conservative and surfaces parser bugs. Bad, because
  broken but common files fail.
- **C — Never automatic.** Good, because behaviour is predictable. Bad, because desktop breadth is weaker.

## References

- [D-05 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/5)
- [ADR-0002](0002-portable-engine-porting-layer-html5-api.md), [ADR-0010](0010-license-and-visibility.md),
  [ADR-0014](0014-dependency-and-licensing-policy.md), [ADR-0015](0015-threading-and-task-runners.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md),
  [ADR-0021](0021-testing-strategy.md)
- [Layering checks](../architecture/layering.md), [port contract](../architecture/port-contract.md),
  [media types](../architecture/media-types.md), [errors](../architecture/errors.md)
- [ISO/IEC 14496-12: ISO base media file format](https://www.iso.org/standard/83102.html),
  [ISO/IEC 23000-19: CMAF](https://www.iso.org/standard/85623.html),
  [ISO/IEC 23009-1: DASH, including `emsg`](https://www.iso.org/standard/83314.html)
- [Apple: QuickTime edit list atom](https://developer.apple.com/documentation/quicktime-file-format/edit_list_atom),
  [Apple: audio priming and encoder delay in AAC](https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFAppenG/QTFFAppenG.html)
- [ITU-T H.222.0: MPEG-2 systems](https://www.itu.int/rec/T-REC-H.222.0)
- [RFC 9559: Matroska](https://www.rfc-editor.org/rfc/rfc9559), [RFC 8794: EBML](https://www.rfc-editor.org/rfc/rfc8794),
  [WebM container guidelines](https://www.webmproject.org/docs/container/)
- [RFC 8216 §3.4: packed audio](https://www.rfc-editor.org/rfc/rfc8216#section-3.4),
  [Apple: timed metadata for HTTP Live Streaming](https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/HTTP_Live_Streaming_Metadata_Spec/Introduction/Introduction.html),
  [ID3 v2.4 structure](https://id3.org/id3v2.4.0-structure),
  [AOM: carriage of ID3 timed metadata in CMAF](https://aomediacodec.github.io/id3-emsg/)
- [ATSC A/53: digital television standard (Part 4, captions in SEI)](https://www.atsc.org/atsc-documents/a53-atsc-digital-television-standard/)
- [RFC 6381: codecs and profiles parameters](https://www.rfc-editor.org/rfc/rfc6381),
  [W3C: MSE byte stream format registry](https://www.w3.org/TR/mse-byte-stream-format-registry/)
- Media3: [`Extractor`](https://github.com/androidx/media/blob/release/libraries/extractor/src/main/java/androidx/media3/extractor/Extractor.java),
  [`SeekMap`](https://github.com/androidx/media/blob/release/libraries/extractor/src/main/java/androidx/media3/extractor/SeekMap.java),
  [MP4 `BoxParser` (edit lists and gapless)](https://github.com/androidx/media/blob/release/libraries/extractor/src/main/java/androidx/media3/extractor/mp4/BoxParser.java),
  [`TimestampAdjuster`](https://github.com/androidx/media/blob/release/libraries/common/src/main/java/androidx/media3/common/util/TimestampAdjuster.java)
- Chromium: [MP4 `track_run_iterator.cc` (edit lists)](https://chromium.googlesource.com/chromium/src/+/main/media/formats/mp4/track_run_iterator.cc),
  [MP2T `timestamp_unroller.h`](https://chromium.googlesource.com/chromium/src/+/main/media/formats/mp2t/timestamp_unroller.h),
  [`ffmpeg_demuxer.h`](https://chromium.googlesource.com/chromium/src/+/main/media/filters/ffmpeg_demuxer.h),
  [The Rule of 2](https://chromium.googlesource.com/chromium/src/+/main/docs/security/rule-of-2.md)
- FFmpeg: [`avio.h` (custom I/O)](https://ffmpeg.org/doxygen/trunk/avio_8h.html),
  [packet side data](https://ffmpeg.org/doxygen/trunk/group__lavc__packet__side__data.html),
  [licence and legal considerations](https://ffmpeg.org/legal.html),
  [OSS-Fuzz project](https://github.com/google/oss-fuzz/tree/master/projects/ffmpeg)
- [VLC: avformat demux module](https://code.videolan.org/videolan/vlc/-/tree/master/modules/demux/avformat)
- [CVE-2015-1538: Stagefright MP4 parsing](https://nvd.nist.gov/vuln/detail/CVE-2015-1538)
