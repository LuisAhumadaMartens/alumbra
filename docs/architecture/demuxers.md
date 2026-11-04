# Demuxers

This page documents the demuxer interface that [ADR-0025](../adr/0025-demuxing-strategy.md) (D-05) decides and the
ISO-BMFF demuxer (#78) that is its first implementation. The MPEG-TS demuxer (#79), the WebM and Matroska demuxer
(#80) and the ADTS, MP3 and ID3 parsers (#333) implement the same interface, so everything under
[The interface](#the-interface) and [Rules for every demuxer](#rules-for-every-demuxer) is their contract.

Everything here is engine-internal. The headers are in `umbra/demux/internal_include/umbra/demux/` and are not
installed; the target is `umbra::demux`. A demuxer lives on one sequence, normally the one that owns the
[data source](data-sources.md), and nothing in it blocks.

## The interface

A demuxer is a **push parser with seek requests**, as in Media3's extractors. The caller appends bytes at known
offsets, calls `Step()`, and the demuxer answers what it needs next. It never reads, never blocks and never owns
a thread, so the single-threaded web build can drive it.

```cpp
class Demuxer {
  void SetSourceInfo(std::optional<uint64_t> size, bool seekable);
  uint64_t NextOffset() const;
  absl::Status Append(uint64_t offset, penumbra::SharedBytes bytes);
  void EndOfInput();
  absl::StatusOr<StepResult> Step();
  absl::StatusOr<uint64_t> Seek(const SeekPoint& point);
  void Reset(uint64_t offset);
};
```

| Member | Contract |
|--------|----------|
| `NextOffset()` | The offset of the first byte the demuxer does not hold yet. The next `Append` must start exactly there, or it fails with `kFailedPrecondition`. After a seek request it is the requested offset. |
| `Append` | Takes ownership of a chunk of any size, down to one byte. A sample that lies inside one chunk reaches the output as a `SharedBytes` slice of it without a copy; a sample that spans chunks is assembled with one copy. |
| `EndOfInput()` | The source has no byte at `NextOffset()` and beyond. A demuxer that needs more bytes then fails with `DataLoss` (the file is truncated). A seek request clears it. |
| `Step()` | One bounded unit of work. Returns a `StepResult` or an error `absl::Status` and, after an error, keeps returning it until `Seek` or `Reset`. |
| `Seek` | Takes a `SeekPoint` from the `SeekMap`, repositions the parser and returns the byte offset the data source must continue at. The first sample of every track after it has `discontinuity` set. |
| `Reset(offset)` | Drops all buffered state except the track description and continues at `offset` of a new resource. HLS and DASH use it to feed one segment after another to the same demuxer. |

### Step results

| Result | Meaning | What the caller does |
|--------|---------|----------------------|
| `kNeedMoreData` | The demuxer holds a prefix and needs `wanted_bytes` more at `byte_offset`, which equals `NextOffset()`. | Reads from the data source at that offset, appends, or calls `EndOfInput()` when the source is empty there. |
| `kSeek` | **Continue at byte offset N.** The demuxer dropped what it held and `NextOffset()` is `N`. | Turns it into a range request at `N` (the [buffered reader](data-sources.md#buffered-reader) restarts the source), then appends the result. |
| `kYield` | The step used its work budget (`kStepBudgetUnits`, 128 units of one box or one sample each). | Posts another step to the task runner, so a single-threaded loop stays within `engine.threads.yield_budget_ms`. |
| `kEndOfStream` | Every sample has been delivered. | Stops; a later `Seek` starts again. |

A `kSeek` is issued only when the target lies behind the buffered window, or more than `kMaxSkipBytes` (256 KiB)
beyond it. A shorter forward skip is asked for as ordinary `kNeedMoreData` and the bytes are discarded as they
arrive. On a source without range support the data source decides whether to serve a seek by discarding or to
fail ([data sources](data-sources.md#data-sources)).

The caller's loop is the whole integration:

```cpp
for (;;) {
  StepResult step = demuxer.Step().value();
  switch (step.kind) {
    case StepKind::kNeedMoreData: {
      bytes = source.Read(demuxer.NextOffset(), chunk);
      if (bytes.empty()) demuxer.EndOfInput();
      else demuxer.Append(offset, bytes);
      break;
    }
    case StepKind::kSeek:      break;
    case StepKind::kYield:     PostNextStep(); return;
    case StepKind::kEndOfStream: return;
  }
}
```

### Output

The demuxer reports through a `DemuxerOutput` that the caller supplies at construction.

| Callback | Carries |
|----------|---------|
| `OnStreams` | The `penumbra::StreamInfo` of every playable track: codec, RFC 6381 string, configuration record, exact time base, language and video or audio description. It is called again when a new initialisation segment changes the tracks. |
| `OnSeekMap` | A `SeekMap`: `IsSeekable()`, `Duration()` and `SeekPointFor(time)`, which returns the time and byte offset of the nearest sync point at or before the target. A map is immutable; the demuxer emits a new one when it learns more (for example when a `sidx` box arrives). |
| `OnSample` | A `penumbra::EncodedSample`, **unchanged from #51**: exact `Timestamp`s in the track's time base, key-frame and discontinuity flags, a `config_change` on the first sample of a new sample description, and `trim_start` and `trim_end`. |
| `OnMetadataStream`, `OnTimedMetadata` | Timed metadata as separate streams, see below. |
| `OnProducerReferenceTime` | A `prft` box: track, NTP timestamp and the matching media time. Nothing else produces live-latency references. |

`codec_config` of a stream is the configuration record as the container stores it: the `avcC`, `hvcC`, `av1C`,
`vpcC`, `dOps`, `dac3`, `dec3` or `dfLa` payload, and the `AudioSpecificConfig` bytes for AAC. The codec string and
the audio and video descriptions come from [`umbra::codec`](codec-config.md).

### Timed metadata streams

Captions, `emsg` and ID3 never travel on `EncodedSample` ([ADR-0025](../adr/0025-demuxing-strategy.md#captions-emsg-and-id3-leave-as-timed-streams)).
A `MetadataStreamInfo` announces a stream before its first item: `stream_id` (a separate id space from track ids),
`kind` (`kEmsg`, `kId3` or `kCaptions`), the time base, the `emsg` scheme and value, and the source track for
captions. Each `TimedMetadataItem` has a `stream_id`, a `time`, an optional `duration` (unknown means none given),
the payload as `SharedBytes` and the event id.

- **Captions.** `ExtractCaptionData` scans the length-prefixed NAL units of an H.264 or HEVC sample for
  `user_data_registered_itu_t_t35` SEI messages with the ATSC `GA94` identifier and returns the `cc_data` triplets
  of each. The sample bytes are untouched. The item time is the sample's `pts` in decode order; #136 reorders. The
  function works on sample bytes, so the MPEG-TS demuxer and the fallback demuxer's samples use it too.
- **`emsg`.** Version 1 items carry the absolute presentation time. A version 0 box is relative to the earliest
  presentation time of the fragment that follows it, so the demuxer holds it until that `moof` arrives. A box that
  is still waiting at the end of the stream is delivered with an unknown time. The AOM ID3 scheme
  (`https://aomedia.org/emsg/ID3`) yields `kId3`. Other versions are skipped and counted.
- **ID3.** The MPEG-TS demuxer (stream type `0x15`) and the packed-audio parsers produce `kId3` streams the same way.

### Errors

Errors are `absl::Status` values from the 3xxx demux range of the [error catalogue](errors.md): `DataLoss`
(`MediaCorrupt`, 3001) for anything malformed or truncated, and `Unimplemented` for a well-formed feature that is
not supported (no playable track, an unknown `emsg` version is skipped instead). The demuxer attaches no
`ErrorDetail` because it does not know the URL; its caller wraps the status. A rejection before the first sample
is the trigger for the desktop libavformat fallback ([fallback demuxer](fallback-demuxer.md)); after the first sample the demuxer never asks for it.

## Rules for every demuxer

1. **Never block and never read.** Ask for bytes through `StepResult` only.
2. **Resume at any byte.** `Append` may deliver one byte at a time; the test suite feeds every format one byte at
   a time and compares the output with a single-chunk run.
3. **Bound everything.** A size field read from the input is checked against the bytes that remain, against a
   constant limit, and before any allocation. Nesting has a depth limit. Read through the bounds-checked
   `ByteReader` of [byte readers](byte-readers.md).
4. **Do the work in small steps.** One unit of work per loop turn, and `kYield` when the budget is used.
5. **Normalise timestamps in the parser.** The output is exact integer time in the track's time base. Rounding
   uses `Rescale` with an explicit mode.
6. **Flag, do not hide.** Set `discontinuity` on the first sample after `Seek` or `Reset`, and where the stream's own
   timeline jumps. Set `config_change` when the codec configuration changes.
7. **Leave the sample alone.** Do not rewrite sample bytes; extract metadata beside them.
8. **Ship a fuzz target** with seed corpora, registered with `alumbra_add_fuzz_target`.

## ISO-BMFF

`Mp4Demuxer` reads progressive MP4, QuickTime-style files and fragmented MP4 or CMAF with the same code.

### Structure

| Part | Behaviour |
|------|-----------|
| Boxes | Compact, `largesize` and `uuid` headers. A box of size zero extends to the end of the file. `moov`, `moof`, `sidx`, `emsg` and `prft` are buffered whole (at most 256 MiB) and parsed through the bounds-checked reader; `mdat` and unknown boxes are skipped by size. The first box must be a known top-level type. |
| Limits | Nesting depth 16, 32 tracks, 64 sample entries per track, 4 Mi samples per track and 8 Mi per movie, 256 MiB per sample. A count that cannot fit in its box fails before anything is allocated. |
| Sample entries | `avc1`, `avc3`, `hvc1`, `hev1`, `vp09`, `av01`, `mp4a`, `Opus`, `ac-3`, `ec-3` and `fLaC`, with `avcC`, `hvcC`, `vpcC`, `av1C`, `esds`, `dOps`, `dac3`, `dec3` and `dfLa` parsed by `umbra::codec`, plus `pasp`, `colr` (`nclx`), `mdcv`, `clli`, `dvcC` and `dvvC`. Colour and HDR values are reported in `VideoInfo`; malformed optional metadata boxes are ignored. `mp4a` with object type `0x6B` or `0x69` is MP3. QuickTime sound entry versions 0, 1 and 2 are read. A track with another entry (text, timed metadata) is skipped and counted, and a file with no playable track is rejected. |
| Sample tables | `stts`, `ctts` (versions 0 and 1), `stsc`, `stsz`, `stz2`, `stco`, `co64` and `stss`, expanded once into a compact list of offset, size, duration, decode time, composition offset, description and sync flag. Offsets are 64-bit throughout, and an offset sum that overflows is an error. |

### Opening a file

A file with `moov` before `mdat` parses `moov` and starts delivering. A file with `moov` after `mdat` is opened
with a few range requests:

1. The first read at offset 0 returns `ftyp` and the `mdat` header. The demuxer skips `mdat` by its size, which is
   a `kSeek` to the end of the box.
2. The read there returns `moov`, which is parsed whole.
3. Delivery starts at the first sample, which is a second `kSeek`, back to the beginning of `mdat`.

The test with a 1.2 MB file behind the buffered reader opens with at most three range requests and fetches less
than the file. An `mdat` with size zero and an unknown source size cannot be skipped and is read to the end.

Progressive samples are delivered **in file offset order**, merged across tracks, so an interleaved file is read
forward without seeking. A gap of up to 256 KiB is read and discarded, a larger one or a step back is a `kSeek`.
Samples of a track that lie in a far-away chunk (a file with audio stored after the video) therefore cost a
seek per switch, which is the minimum for that layout. A `co64` offset above 4 GiB is a plain `kSeek` to that
offset and is covered by a sparse-file test.

### Fragments

`mvex` and `trex` give the defaults. For each `moof` the demuxer reads `tfhd` (base offset, description,
default duration, size and flags, `default-base-is-moof`), `tfdt` and every `trun` (data offset, first-sample
flags, duration, size, flags and composition offset, signed in version 1). The legacy base rule (the first track
fragment starts at the `moof`, later ones continue where the previous ended) is supported. A fragment without
`tfdt` continues from the previous fragment's end. A `tfdt` that differs from that end sets `discontinuity`.

The samples of a fragment are delivered when the `mdat` that follows is reached, in file order, and parsing then
continues after that `mdat`. A fragment whose data lies before the `moof` is delivered at once. A second `moov`
(a new initialisation segment) replaces the tracks, calls `OnStreams` again and puts a `config_change` on the next
sample of each track.

CMAF segments arrive as `styp`, optional `sidx`, `moof` and `mdat`, without `moov`. The HLS and DASH layers call
`Reset(0)` between segments after the initialisation segment has been parsed. Sample offsets are relative to the
`moof`, so they do not depend on where the resource starts.

### `sidx`, `emsg` and `prft`

- **`sidx`** builds the seek map. The anchor is the end of the box plus `first_offset`, and each reference adds its
  size and duration. References to another `sidx` (hierarchical indexes) are entries too, and the nested index
  replaces them when it is parsed. Only the first reference id that matches a track is used. The map duration is
  the end of the last reference. Without a `sidx`, a fragmented file reports a map that is not seekable, and the
  HLS and DASH layers seek by segment.
- **`emsg`** and **`prft`** are described above and reach the output as timed metadata and a producer reference.

### Timestamps

The demuxer normalises timestamps at the source, with integer arithmetic in the track's own time base.

- **Edit list.** ADR-0025's single-edit rule: leading empty edits add up to a **delay**; the first non-empty
  entry's `media_time` is the **start offset**; every later entry is ignored and counted
  (`Mp4DemuxerStats::ignored_edit_entries`, until the logging layer exists to report it). Every `dts` and `pts` is
  shifted by `delay - media_time`, so a stream whose first sample must be dropped by two frames shows a negative
  `dts`. Durations in the movie timescale are converted with `Rescale` and `kNearest`.
- **Priming and padding.** For audio, samples before the edit start stay in the stream, and `trim_start` is how
  much of the sample lies before presentation time zero (the whole sample for a sample that lies wholly before).
  The edit's end, when `segment_duration` is non-zero, gives `trim_end` the same way. Video is shifted but never
  trimmed: frames before the start are pre-roll for the decoder. AAC with 1024 samples of priming therefore has
  `pts = -1024` and `trim_start = 1024` on its first sample, and `pts + trim_start` is zero.
- **Composition offsets.** `ctts` and `trun` offsets are carried exactly. Version 1 values are signed, so a `pts`
  may precede its `dts`; version 0 values above 2^31 are read as signed too, as other demuxers do.
- **Disabling.** `demux.mp4.edit_lists.enabled` (boolean, default `true`, applied at the next load) is the
  escape hatch of the [settings schema](settings.md). `Mp4DemuxerOptions::edit_lists_enabled` carries it; with it
  off, nothing is shifted or trimmed. The settings store is wired to the option where the demuxer is created, with
  the load pipeline.

### Seeking

For a progressive file `SeekPointFor(t)` finds, in the reference track (the first video track, else the first),
the last sync sample whose `pts` is at or before `t`, and returns its time rounded up to a microsecond so that the
point converts back to the same sample. The byte offset is the smallest offset any track needs. `Seek` repositions
every track at the sync sample at or before the time, so audio starts slightly earlier than video and the pipeline
drops what it does not need. For a fragmented file the point is a fragment start from the `sidx`, and `Seek`
returns that offset.

## ADTS, MPEG audio and ID3

`AdtsDemuxer`, `Mp3Demuxer`, `Ac3Demuxer` and `Eac3Demuxer` implement the
packed-audio path as bounded push parsers. They resynchronise on frame headers,
preserve each complete frame as an `EncodedSample`, and derive frame durations
from the decoded sample count and stream sample rate. ADTS announces an AAC
stream with an AudioSpecificConfig built from the first frame header and
validated by `ParseAudioSpecificConfig`. MPEG audio Layer III announces the MP3
codec string and uses the MPEG version and sample rate to choose 1,152 or 576
samples per frame. AC-3 and E-AC-3 reuse the sync-frame framing and codec
configuration builders used by the MPEG-TS demuxer.

An ID3 `PRIV` frame owned by
`com.apple.streaming.transportStreamTimestamp` anchors the first audio frame to
its 33-bit, 90 kHz timestamp. The packed-audio demuxer unwraps that timestamp
across segment resets, so packed audio can share a timeline with TS or fMP4
video.

The seek map records each parsed frame boundary, so a seek after parsing has
reached its target starts at a frame no later than the requested time. Xing,
Info and VBRI metadata frames are skipped as audio and their frame counts
provide duration; otherwise duration is the sum of parsed frame durations.
LAME and Lavc encoder delay and padding are mapped to `trim_start` and
`trim_end` without shifting the audio timestamps.

ID3v2 tags and an ID3v1 trailer are surfaced as `kId3` timed-metadata payloads.
The original tag bytes are retained, allowing consumers to interpret title,
artwork and other frames without changing audio samples or their timing. ID3v2
tags are capped at 16 MiB and malformed synchsafe lengths are rejected. ID3v1
trailers are recognised at end of input, including when the source size is
unknown.

| Test | What it proves |
|------|----------------|
| `packed_audio_demuxer_test` | ADTS configuration and timing, ID3 extraction, MP3 frame timing, duration and frame-boundary seeking with one-byte input. |
| `packed_audio_demuxer_fuzzer` | ADTS, MP3, AC-3, E-AC-3 and ID3 parsing under libFuzzer, with valid and truncated seed files. |

## Verification

| Test | What it proves |
|------|----------------|
| `mp4_demuxer_test`, `mp4_fragment_test` | Exact timing, edit lists, signed composition offsets, `co64`, `stz2`, chunk runs, key frames, seeking, `sidx`, `emsg`, `prft`, captions, segments after an initialisation segment, one-byte feeding, zero-copy slices, the step budget, and that every truncation and every corrupted byte gives samples or `DataLoss`. |
| `mp4_box_test` | Header forms, size and nesting limits, `esds` forms and the malformed-file cases: truncated or oversized boxes, size overflow, children larger than their parent, excessive nesting, impossible counts, zero timescale. |
| `mp4_source_test` | The demuxer behind the real `BufferedReader` and the fake data source: `moov` at the end opens with at most three range requests. |
| `mp4_golden_test` | Generated clips (`test/umbra/demux/fixtures/generate.sh`, FFmpeg `lavfi`, bit-exact) compared with `ffprobe -show_packets`: `pts`, `dts`, duration, size and key flag of every packet of a progressive file with `moov` last, a fast-start file, a fragmented file with `sidx`, an AAC file with priming and a six-codec file. |
| `mp4_demuxer_fuzzer` | The whole demuxer under libFuzzer, with seeds from the clips. |

The golden files are the `ffprobe` output formatted by `fixtures/make_golden.py`, with two exceptions that are
documented in the file: the last AC-3 and E-AC-3 packet durations, which `ffprobe` takes from its parser (1536)
and the container states as 384. A CMAF-flavoured clip is not a golden case because FFmpeg's generic layer shifts
`pts` by one frame when the muxer omits composition offsets from the first fragment, and the file itself says
otherwise. None of the blocking tests uses the network.

## MPEG-TS

`TsDemuxer` reads MPEG-2 transport streams (ITU-T H.222.0): `.ts` files, HLS segments and broadcast captures. It
is a push parser like `Mp4Demuxer`, in `umbra/demux/ts_*.cc`. The format-specific code lives in `ts_packet`
(packets, tables and PES headers), `ts_framing` (access-unit framing and configuration records), `ts_seek_map` and
`ts_demuxer` (the state machine).

### Packets and recovery

| Part | Behaviour |
|------|-----------|
| Synchronisation | The demuxer locks on five sync bytes (`0x47`) at one packet size. It accepts 188-byte packets, 192-byte M2TS packets (a four-byte timecode before each packet) and 204-byte packets (parity after each packet). The lock scans a 4 KiB window. |
| Lost sync | A packet that does not start with `0x47` drops every partial PES and table section, forgets the continuity counters, flags a discontinuity on every stream and scans for a new lock. Garbage before the first lock is skipped without counting as a loss. |
| Continuity | A payload packet whose counter is not the previous one plus one is a loss unless the adaptation field sets `discontinuity_indicator`. A loss drops the partial PES of that PID and flags the next sample. A packet that repeats the previous counter is a duplicate and is ignored. |
| Damaged packets | A packet with `transport_error_indicator` is dropped like a loss. Scrambled packets are counted and skipped. A malformed adaptation field counts as a corrupt packet. |
| Indicators | `discontinuity_indicator` on a stream's PID flags that stream. On the PCR PID it flags every stream. The PCR is parsed and counted, and it does not drive timing: the demuxer takes every time from PES headers. |

`TsDemuxerStats` counts each of these (`sync_losses`, `continuity_errors`, `duplicate_packets`,
`transport_errors`, `scrambled_packets`, `crc_errors`, `dropped_pes`, `pmt_changes` and others). The logging layer
reports them when it exists.

### Tables and programs

- **Sections.** The reassembler follows `pointer_field`, handles several sections in one packet, limits a section to
  4096 bytes and checks the CRC-32. A section with a bad CRC is ignored, counted, and flags a discontinuity on every
  stream, because damaged tables mean damaged data around them.
- **Program selection.** `demux.ts.program_number` (default 0) picks the program. Zero selects the first program of
  the PAT that is not the network entry. Another value selects that program number. A stream whose program never
  appears fails with `DataLoss`, because it has no usable tables.
- **PMT changes.** A PMT that lists the same PIDs, stream types and descriptors as before is ignored, so a table that
  repeats every 100 ms costs nothing. A PMT that differs completes the partial PES of each stream, flushes the held
  samples, builds the new stream list, calls `OnStreams` again once every stream has a configuration, and puts a
  discontinuity and a `config_change` on the next sample of each stream that stays. Tables are not applied while a
  seek is running.

### Stream types

| Type | Stream | Output |
|------|--------|--------|
| `0x1B` | H.264, Annex B | Track, `avcC` as `codec_config` |
| `0x24` | HEVC, Annex B | Track, `hvcC` as `codec_config` |
| `0x0F` | AAC in ADTS | Track, two-byte `AudioSpecificConfig` built from the ADTS header |
| `0x03`, `0x04` | MPEG audio layers I to III | Track, codec string `mp4a.6B` (MPEG-1) or `mp4a.69` (MPEG-2 and 2.5), no configuration |
| `0x81`, `0x87` | ATSC AC-3 and E-AC-3 | Track, `dac3` or `dec3` built from the first sync frame |
| `0x06` | Private data with an AC-3 (`0x6A`) or E-AC-3 (`0x7A`) descriptor, or an `AC-3` or `EAC3` registration descriptor | The same as `0x81` and `0x87` |
| `0x15` | ID3 timed metadata | `kId3` metadata stream, see below |

Other types (MPEG-2 video, teletext, DVB subtitles, LATM) are skipped and counted in `skipped_streams`. The language
comes from the ISO 639 descriptor. The track id is the PID, so ids stay stable across PMT changes.

### Access units and samples

The sample bytes are **not rewritten**, as for every demuxer. `StreamInfo` declares Annex B framing for H.264 and
HEVC and ADTS framing for AAC, while the codec configuration remains `avcC`, `hvcC` or AudioSpecificConfig. A video
sample is one PES payload in Annex B form. An
audio sample is one ADTS frame (header included), MPEG audio frame or AC-3 sync frame, and an E-AC-3 independent
frame keeps its dependent substreams. The configuration record is still delivered, so the decoder layer can
configure itself. Desktop FFmpeg converts Annex B samples to the NAL length size declared by the codec
configuration and removes ADTS headers before passing AAC access units with AudioSpecificConfig.

- **Video.** One PES is one access unit. The Annex B scan finds parameter sets, SEI messages and IDR (H.264) or
  IRAP (HEVC) slices. A key frame is an IDR or IRAP access unit, or one whose packet sets
  `random_access_indicator`. The configuration comes from the in-band SPS, PPS and VPS through `umbra::codec`, so
  the codec string, size, aspect ratio and colour description match the MP4 demuxer's. Access units before the
  first parameter set are dropped. A parameter set that changes the record puts a `config_change` on its sample.
- **Duration.** A video sample's duration is the difference to the next DTS, so each video sample is held back until
  the next one arrives. The last one is released at the end of input, a seek or a reset with the previous duration.
  An audio sample's duration comes from its sample count, and the start times inside a PES are the PES time plus
  the rounded sum of the earlier frames, so a long PES never accumulates rounding error.
- **Audio.** ADTS, MPEG audio and AC-3 sync words resynchronise inside a PES. Bytes that are not a frame, and a frame
  that is cut off, are dropped and flag the next sample.
- **Announcement.** The streams are announced once every media stream has a configuration, or after 40 000 packets
  without one, when the streams that still lack a configuration are dropped and counted. The samples and metadata
  that arrive meanwhile are held in order, so no sample precedes `OnStreams`.

### Timestamps

All times are exact integers on the 90 kHz clock.

- **Wrap.** Each 33-bit PTS and DTS is unwrapped into 64 bits against the previous value of the demuxer, choosing the
  nearest candidate, as [ADR-0025](../adr/0025-demuxing-strategy.md) decides. A header's DTS is unwrapped before
  its PTS, so a PTS that wraps just after its DTS lands on the right side. Nothing is subtracted: a stream that
  starts at 10 s reports 10 s.
- **Discontinuities.** A sample is flagged when its time differs from the previous time of its stream by more than
  10 s in either direction (`kTsDiscontinuityTicks`, a constant and not a setting, as the ADR requires), when the
  stream had a loss, a bad table or a `discontinuity_indicator`, and on the first sample after `Seek` or `Reset`.
  HLS `EXT-X-DISCONTINUITY` stays with the HLS layer.
- **Limit.** The nearest-candidate rule cannot tell a forward jump of more than about 13 hours from a wrap, so the
  duration of a file longer than that is measured modulo 26.5 hours.

### Timed metadata

- **Captions.** When an access unit has an SEI NAL unit, the demuxer makes a length-prefixed copy and passes it to
  `ExtractCaptionData`, the same function as the MP4 demuxer uses. The sample is untouched. Items have the PTS of the
  access unit. `TsDemuxerOptions::extract_captions` switches it off.
- **ID3.** Each PES of a `0x15` stream is one `kId3` item with the whole payload, the PES time and an unknown
  duration. The stream is announced with the first item.

### Opening and seeking

A transport stream has no index, so the demuxer builds one from timestamps.

- **Opening.** The demuxer reads from the start until the streams are announced, which fixes the first timestamp.
  When the source size is known and the source supports ranges, it then asks for the last 1 MiB (`kTsTailBytes`)
  with a `kSeek`, takes the largest PTS in it, and asks for the start again. That is three range requests, and
  nothing is delivered before the last one, so no sample repeats. The map is seekable and its duration is the
  largest PTS minus the first DTS. When the file is small enough that the first read already covers the tail, the map
  arrives at the end of the stream instead. Without a size or without range support, the map is not seekable and the
  duration is unknown. `TsDemuxerOptions::probe_duration` turns the tail read off, and HLS sets it to false for the
  segments that `Reset` feeds one after another.
- **Seek points.** `SeekPointFor(t)` returns `t` clamped to the stream and a byte offset interpolated linearly
  between the first packet and the end. This is an estimate, unlike the MP4 map, and `Seek` refines it.
- **Bisection.** `Seek` bisects over byte ranges. A probe reads from an offset to the first key frame of the
  reference stream (the first video stream, else the first stream) and compares its PTS with the target. The
  bracket keeps the lowest offset whose key frame is after the target and the highest offset whose key frame is at
  or before it, and each probe is a `kSeek` to the middle. The search stops when the bracket is under 256 KiB
  (`kTsSeekGranularity`), after at most `ceil(log2(size / 256 KiB))` probes. The demuxer then asks for the lower
  edge, drops video until the first key frame and flags the first sample of every track. A target at or before the
  first timestamp starts at the beginning of the stream.

### Verification of the MPEG-TS demuxer

| Test | What it proves |
|------|----------------|
| `ts_packet_test` | The CRC-32 check value, packet and adaptation parsing with PCR, section reassembly across packets and within one, PES headers and marker checks, unwrapping across several wraps, and the ADTS, AC-3, MPEG audio and Annex B framing. |
| `ts_demuxer_test` | Exact times and flags, one-byte feeding, bounded and unbounded PES, a stream that crosses 2^33, a lost packet, a duplicate, a discontinuity indicator, lost sync, a bad PMT CRC, transport errors, PMT changes, parameter set changes, program selection, captions, ID3, AC-3 carriage, timestamp jumps, `Reset`, the step budget, 192-byte and 204-byte packets, every truncation and corrupted byte, and shuffled packets. |
| `ts_golden_test` | Generated clips (`test/umbra/demux/fixtures/generate_ts.sh`, FFmpeg `lavfi`, bit-exact) against `ffprobe -show_packets`: H.264 with AAC, HEVC with AC-3, and MPEG audio with E-AC-3. The last duration of each stream is not compared, because `ffprobe` derives it from codec parameters. |
| `ts_source_test` | A 7 MiB stream behind the real `BufferedReader` and the fake data source: the duration comes from the tail with at most four range requests, and seeking needs at most `ceil(log2(size / 256 KiB)) + 3` more, asserted from the read log. Seeks back and forth, past the end, to the start and without a size are covered. |
| `ts_demuxer_fuzzer` | The whole demuxer under libFuzzer, with seeds from the clips and the packet-size, lost-sync and program variants. |

## Follow-ups

- **#79 (MPEG-TS):** done, see [MPEG-TS](#mpeg-ts). `Reset` is the segment entry point for HLS, which sets
  `probe_duration` to false. #108 reuses the AC-3 and E-AC-3 framing of `ts_framing.cc` for packed audio, and #333
  can fold the minimal ADTS and MPEG audio framing there into its own parsers.
- **#80 (WebM and Matroska):** implement `Demuxer` with Cues in the seek map and a `kSeek` to Cues at the end of
  the file, `trim_start` and `trim_end` from CodecDelay and DiscardPadding, and encryption metadata beside the
  samples.
- **#333 (ADTS, MP3, ID3):** implement `Demuxer` with Xing and VBRI in the seek map, LAME priming in
  `trim_start`, and ID3 frames as `kId3` streams.
- **Pipeline:** a driver that connects `Demuxer` to the buffered reader and posts `kYield` steps, and wires
  `demux.mp4.edit_lists.enabled`.
- **#236:** `senc`, `saiz`, `saio`, `pssh` and `tenc` into `EncryptionInfo` (the boxes are skipped today).
