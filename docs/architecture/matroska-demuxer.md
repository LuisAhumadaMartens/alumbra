# WebM and Matroska demuxer

`MatroskaDemuxer` is the WebM and Matroska implementation (#80) of the push-parser [`Demuxer` interface](demuxers.md#the-interface)
that [ADR-0025](../adr/0025-demuxing-strategy.md) (D-05) decides. The interface, the step results, the output
callbacks and the [rules every demuxer follows](demuxers.md#rules-for-every-demuxer) are the same as for ISO-BMFF, so
this page documents only what is specific to EBML and Matroska. The headers are in
`umbra/demux/internal_include/umbra/demux/` and are not installed.

The parser follows RFC 9559 (Matroska) on top of RFC 8794 (EBML). It reads the WebM subset and the Matroska
features that the ADR lists for v1. It never blocks, never reads from a source and works on any append chunk size,
down to one byte.

## Structure

| Part | Behaviour |
|------|-----------|
| EBML header | Buffered whole (at most 64 KiB). `DocType` must be `webm` or `matroska`, `EBMLReadVersion` must be 1, `EBMLMaxIDLength` at most 4, `EBMLMaxSizeLength` at most 8 and `DocTypeReadVersion` at most 4. Another `DocType` or a newer version fails with `Unimplemented`, and a file that does not start with an EBML header fails with `DataLoss`. Both are rejections before the first sample, so the desktop libavformat fallback of ADR-0025 can take over. |
| Segment | The size may be unknown. Its children are `SeekHead`, `Info`, `Tracks`, `Cues` and `Cluster`; `Tags`, `Chapters`, `Attachments`, `Void`, `CRC-32` and unknown elements are skipped by size. |
| Buffered elements | `SeekHead` (1 MiB), `Info` (1 MiB), `Tracks` (16 MiB), `Cues` (32 MiB) and every block element (256 MiB) are read whole and parsed through the bounds-checked `EbmlReader`. An element whose size exceeds its limit, or lies outside its parent, is treated as unreadable data (see [resynchronisation](#resynchronisation)). |
| Clusters | Entered, not buffered. A cluster of known size ends at its size. A cluster of **unknown size** (live WebM) ends at the next level-one element (`Cluster`, `Cues`, `Tags` and so on) or at the end of the input. A segment of unknown size ends at the end of the input. |
| Limits | 32 tracks, 1 Mi cue points, 64 Ki scanned cluster points, 256 frames per laced block and a 16 MiB resynchronisation scan. No recursion is used: every parser handles a fixed set of element identifiers, so nesting is bounded by the format and unknown masters are skipped. |
| Values | Identifiers have one to four bytes, sizes one to eight. Unsigned and signed integers have up to eight bytes, floats four or eight and strings are cut at the first NUL. A timestamp scale of zero or above 2^63 fails. |

## Opening a file

The demuxer parses `SeekHead`, `Info` and `Tracks` as they arrive and announces the streams when it has both `Info`
and `Tracks`, or at the first cluster if `Info` is missing (the timestamp scale then defaults to 1 ms). A cluster
before the tracks fails with `DataLoss`. A file left with no playable track fails with `Unimplemented`.

**Cues at the end.** When the `SeekHead` names the `Cues` and the file is seekable, the demuxer asks for them at the
first cluster. A position within `kMatroskaMaxSkipBytes` (256 KiB) of the buffered bytes is read forward, and a
farther one is a `kSeek` to the `Cues`, followed by a second `kSeek` back to the first cluster. A source that is not
seekable, a position that is not a `Cues` element and a position beyond the end of the input all fall back to reading
on without cues. A `Cues` element that arrives in file order is used as it comes.

## Tracks and codecs

Tracks come from `TrackEntry` elements. The stream's `track_id` is the Matroska track number, and the `time_base` is
`TimestampScale` nanoseconds per tick (1/1000 by default), except for Opus, which uses 1/48000 so that `CodecDelay`,
packet durations and trimming are exact.

| `CodecID` | Stream | `codec_config` | Parsed by |
|-----------|--------|----------------|-----------|
| `V_MPEG4/ISO/AVC` | H.264 | `CodecPrivate` (the `avcC` record) | `umbra::codec` `ParseAvcC`, SPS |
| `V_MPEGH/ISO/HEVC` | HEVC | `CodecPrivate` (the `hvcC` record) | `ParseHvcC`, SPS |
| `V_VP8` | VP8 | empty | Container dimensions |
| `V_VP9` | VP9 | A `vpcC` record built from the optional `CodecPrivate` fields (profile, level, bit depth, chroma) and the `Colour` element | `SerializeVpcC`, codec string |
| `V_AV1` | AV1 | `CodecPrivate` (the `av1C` record) | `ParseAv1C` |
| `V_THEORA` | Theora | Three Xiph-laced headers in `CodecPrivate` | Xiph header framing and identification header |
| `A_VORBIS` | Vorbis | Three Xiph-laced headers in `CodecPrivate` | Xiph header framing and identification header |
| `A_OPUS` | Opus | `CodecPrivate` (the `OpusHead` header) | `ParseOpusHead` |
| `A_AAC` | AAC | `CodecPrivate` (the `AudioSpecificConfig`) | `ParseAudioSpecificConfig` |
| `A_FLAC` | FLAC | `CodecPrivate` (`fLaC` and the metadata blocks) | `ParseFlacStreamInfo` |
| `A_AC3`, `A_EAC3` | AC-3, E-AC-3 | empty | The audio element gives rate and channels |
| `A_MPEG/L3`, `A_MPEG/L2` | MP3 | empty | The audio element gives rate and channels |
| `S_TEXT/ASS`, `S_TEXT/SSA` | ASS, SSA | `CodecPrivate` (script header and style/event formats) | `AssParser::ParseMatroskaEvent` consumes block event fields with sample timing |

`codec_config` is the record as the container stores it, as for ISO-BMFF, except for VP8, which needs no record, and VP9, where Matroska has no
`vpcC`, so the demuxer builds the same record an MP4 file carries. Vorbis and Theora retain their validated Xiph-laced headers for the decoder. Without a level in the `CodecPrivate` the VP9
level comes from the picture size. Video dimensions, the sample aspect ratio from `DisplayWidth` and `DisplayHeight`,
the colour description from `Colour` and the frame rate from `DefaultDuration` fill the `VideoInfo`.

A track is **skipped and counted** (`MatroskaDemuxerStats::skipped_tracks`) when it is not audio, video or one of
the ASS/SSA subtitle tracks above, when its codec is not in the table, or when its `ContentEncodings` are not
supported. A codec in the table with a broken configuration fails the file with `DataLoss`.

| `ContentEncoding` | Behaviour |
|-------------------|-----------|
| Header stripping (`ContentCompAlgo` 3) | The stripped bytes are put back in front of every frame, which copies the frame once. |
| Other compression (zlib, bzlib, lzo) | The track is not offered, as ADR-0025 decides. |
| Encryption | The track is not offered. `ContentEncryption` and the WebM encryption metadata are #236. |
| More than one encoding | The track is not offered. |

## Blocks, lacing and timestamps

`SimpleBlock` and `BlockGroup` (`Block`, `BlockDuration`, `ReferenceBlock`, `DiscardPadding`) are read. A block
for a track that is not offered is dropped and counted (`dropped_blocks`). A block before the cluster's `Timestamp`
fails with `DataLoss`.

**Lacing.** Xiph, EBML and fixed lacing are split into one sample per frame. Every size is checked against the
block, and a fixed lace must divide the block evenly. A frame that lies inside one appended chunk reaches the output
as a `SharedBytes` slice without a copy. Empty frames are dropped.

**Timestamps.** The timestamp of a frame is the cluster timestamp plus the signed 16-bit offset of the block, scaled
to the track's time base with `Rescale` and `kNearest`. `dts` equals `pts` because Matroska stores no decode time.
Frames in a laced block follow each other by their duration, accumulated in nanoseconds so that a duration such as
21.333 ms does not lose precision.

**Durations.** The first of these that applies is used: `BlockDuration` for a single frame; for Opus, the duration
from the packet's table-of-contents byte; the track's `DefaultDuration`; a laced block's `BlockDuration` shared
between its frames. A laced block with no way to know the frame durations fails with `DataLoss`. Any other frame
without one has an unknown duration, which is the case for AAC, MP3, AC-3 and FLAC written without `DefaultDuration`.
The duration of the last frame of such a stream is whatever the muxer wrote, which can be shorter than the codec's
frame, as the golden test shows for MP3 and AC-3.

**Key frames.** Audio frames are all key frames. For video, `SimpleBlock` uses the key flag and a `BlockGroup` is a
key frame when it has no `ReferenceBlock`.

**Priming and padding.** RFC 9559 requires `CodecDelay` to be subtracted from every block timestamp, and the demuxer
does that for every track that has one. For audio, the part of a frame before presentation time zero becomes
`trim_start`, so an Opus stream with the usual 6.5 ms delay starts with `pts = -312` and `trim_start = 312` in a
1/48000 time base, and `pts + trim_start` is zero. A positive `DiscardPadding` becomes `trim_end` of the last frame of
the block, and a negative one a `trim_start` on the first frame, both in the track's time base. Video is shifted but
never trimmed.

**Discontinuities.** The first sample of every track after `Seek` or `Reset` has `discontinuity` set, and so does the
first sample after a cluster whose timestamp is lower than the previous cluster's.

**New initialisation.** A second EBML header, or a `Tracks` element after the first, replaces the tracks, calls
`OnStreams` again and puts a `config_change` on the next sample of each track. HLS and DASH layers can feed a WebM
initialisation segment and then call `Reset(0)` before each media segment, which may be a bare run of clusters.

## Seeking

Every `Cues` point of the reference track (the first video track, else the first track) becomes a seek point at the
start of its cluster, so a seek lands on a cluster that begins with a key frame and the pipeline drops what it does
not need. A `CuePoint` without a `CueClusterPosition` is ignored. Two cue points with the same time keep the first.

The seek map applies `SeekPreRoll`: a lookup for time *t* returns the last point at or before *t* minus the longest
`SeekPreRoll` of any track (80 ms for Opus), so the decoder has its pre-roll.

**Without cues** the demuxer learns the clusters as it reads them. The seek map is empty (not seekable) until the first
cluster has its timestamp, and then grows by one point per cluster, announced for the first eight clusters and then
every 32 clusters and at the end of the stream, up to 64 Ki points. A seek to a time that is beyond the clusters read
so far returns the last known cluster, and playback continues forward from there. This is the bounded cluster scan of
ADR-0025. A caller that needs to seek far ahead without cues reads forward first.

## Resynchronisation

A damaged or truncated recording can have unreadable bytes inside the segment. When the demuxer finds an element
header that cannot be valid (an identifier longer than four bytes, a size longer than eight), an element that extends
beyond its cluster or segment, an unknown size on an element that cannot have one, or an element above its buffering
limit, it counts a resynchronisation (`MatroskaDemuxerStats::resyncs`), drops the rest of the cluster and scans
forward for the next four-byte level-one identifier. The scan is bounded to 16 MiB; beyond that the file fails with
`DataLoss`. Damage before the segment (in the EBML header) fails at once. Errors inside a buffered element, such as
`Tracks`, are not resynchronised.

## Not supported

| Feature | Behaviour | Follow-up |
|---------|-----------|-----------|
| VP8, Vorbis, Theora and other codecs without a `CodecId` | The track is skipped. A file with only such tracks fails with `Unimplemented`, which is the fallback trigger. | Extend `penumbra::CodecId`, which changes the port contract and `kApiVersion`. |
| Microsoft MPEG-4 Part 2 (`V_MS/VFW/FOURCC`, `MP42` in [Matroska test 6](https://www.matroska.org/downloads/test_suite.html); FFmpeg identifies it as `msmpeg4v2`) | The track is skipped. If no other audio or video track is supported, the `Unimplemented` error names the unsupported codec identifier. The bundled decoder set does not include `msmpeg4v2`. | Add a reviewed decoder and capability mapping if this codec enters the supported set. |
| Subtitle codecs other than ASS and SSA | Skipped. | #130 and later format work |
| Font attachments, chapters and tags | Skipped by size. | Font attachments are required by the ASS font-provider follow-up; chapters and tags remain deferred. |
| Ordered chapters and linked segments | The physical segment plays in storage order. | ADR-0025 |
| Encryption | The track is not offered. | #236 |
| `BlockAdditions`, `EncryptedBlock`, `SilentTracks`, `Position` | Ignored. | |
| Caption extraction from H.264 and HEVC samples | Not done by this demuxer. | #136 |
| Decode time from reordered H.264 and HEVC | `dts` equals `pts`. | A later change can derive `dts` from the delivery order. |
| Frame durations from codec parsers | Unknown when the container has none. | |

## Verification

| Test | What it proves |
|------|----------------|
| `ebml_test` | Variable-length integers of every length, reserved all-ones values, header lengths, truncated and oversized elements, and the value readers. |
| `mkv_block_test` | Block headers and Xiph, EBML and fixed lacing, including sizes that exceed the block, negative sizes and truncated lace headers. |
| `mkv_demuxer_test` | Streams and codec descriptions, exact timestamps, key frames, `BlockGroup`, `DiscardPadding`, lacing through the whole demuxer, unknown-size clusters and segments, cues before and after the clusters, the two seeks to far cues, seeking with and without cues, `SeekPreRoll`, header stripping, skipped tracks, `DocType` checks, scale, a changed initialisation segment, `Reset` with bare clusters, resynchronisation, one-byte feeding, the step budget, and that every truncation and every corrupted byte gives samples or an error. |
| `mkv_golden_test` | Generated clips (`test/umbra/demux/fixtures/generate_matroska.sh`, FFmpeg, bit-exact) compared with `ffprobe -show_packets`: a VP9 and Opus WebM file with cues, the same file with unknown-size clusters and no cues, and a Matroska file with H.264, HEVC, VP9, AV1, AAC, FLAC, MP3, AC-3, E-AC-3 and Opus. |
| `mkv_demuxer_fuzzer` | The whole demuxer under libFuzzer, with seeds that include unknown sizes, lacing, damaged data and a changed initialisation segment. |

The golden files use `ffprobe` output formatted by `fixtures/make_golden.py`. The comparison covers the `pts` in
milliseconds, the size and the key flag of every packet, and the duration of every packet except the last of a
stream. `ffprobe` rounds an Opus `CodecDelay` of 6.5 ms to 7 ms before it subtracts it, so the test allows one
millisecond on Opus. `dts` is not compared because Matroska has no decode time. The last-packet durations of MP3 and
AC-3 differ because `ffprobe` takes them from its parsers.

The eight files of the Matroska test suite were checked by hand with the same comparison, outside CI, because they
are about 200 MB and their licence is not in the conformance manifest. `test1` to `test3` and `test5` to `test8`
parse without error and match `ffprobe` on the `pts`, size and key flag of every packet of the tracks the demuxer
offers, apart from 13 to 25 laced AAC packets per file that are 2 ms later than `ffprobe` shows (the demuxer places
frame *n* of a lace at *n* times the exact `DefaultDuration`, and `ffprobe` truncates the frame duration to whole
milliseconds first). The video of `test1` and `test6` is MS-MPEG4 and the subtitles of `test5` are text, so those
tracks are skipped. `test7` has damaged data inside a cluster and `test4` damaged data at the start; both are crossed
by resynchronisation. `test4` contains Theora and Vorbis only, so it fails with `Unimplemented` once its tracks are
read.
