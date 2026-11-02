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
is the trigger for the desktop libavformat fallback; after the first sample the demuxer never asks for it.

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
| Sample entries | `avc1`, `avc3`, `hvc1`, `hev1`, `vp09`, `av01`, `mp4a`, `Opus`, `ac-3`, `ec-3` and `fLaC`, with `avcC`, `hvcC`, `vpcC`, `av1C`, `esds`, `dOps`, `dac3`, `dec3` and `dfLa` parsed by `umbra::codec`, plus `pasp` and `colr`. `mp4a` with object type `0x6B` or `0x69` is MP3. QuickTime sound entry versions 0, 1 and 2 are read. A track with another entry (text, timed metadata) is skipped and counted, and a file with no playable track is rejected. |
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

## Follow-ups

- **#79 (MPEG-TS):** implement `Demuxer` with the 33-bit unwrap, the discontinuity threshold, bisection seeking
  through `kSeek` and a `SeekMap`, and stream type `0x15` as a `kId3` stream. `Reset` is the segment entry point
  for HLS. Reuse `ExtractCaptionData`.
- **#80 (WebM and Matroska):** implement `Demuxer` with Cues in the seek map and a `kSeek` to Cues at the end of
  the file, `trim_start` and `trim_end` from CodecDelay and DiscardPadding, and encryption metadata beside the
  samples.
- **#333 (ADTS, MP3, ID3):** implement `Demuxer` with Xing and VBRI in the seek map, LAME priming in
  `trim_start`, and ID3 frames as `kId3` streams.
- **Pipeline:** a driver that connects `Demuxer` to the buffered reader and posts `kYield` steps, and wires
  `demux.mp4.edit_lists.enabled`.
- **#236:** `senc`, `saiz`, `saio`, `pssh` and `tenc` into `EncryptionInfo` (the boxes are skipped today).
