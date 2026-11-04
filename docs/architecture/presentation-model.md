# Presentation model

`umbra::manifest` (`umbra/manifest/`, namespace `umbra::manifest`) holds the format-agnostic model that the HLS and
DASH parsers produce and the pipeline consumes, as [ADR-0032](../adr/0032-unified-presentation-model.md) (D-10)
decides. ABR ([ADR-0033](../adr/0033-abr-algorithm-and-bandwidth-estimation.md)), buffering
([ADR-0034](../adr/0034-buffer-model-and-memory-budgets.md)) and live logic read this model and never a protocol
type. The module contains no parser: the HLS parser (#106) and the DASH MPD parser (#113) fill it in.

The module is engine-internal. Its headers are in `umbra/manifest/internal_include/umbra/manifest/` and are not
installed. The public track lists (#125) are a separate API built on top of it. No header includes an HLS or DASH
parser header. The model depends on `umbra::base`, `umbra::codec` and the Penumbra interface.

## Shape

```text
Presentation            protocol, static or dynamic, duration, LiveInfo, ad breaks, generation
  Period                id, start, duration, trick-play list
    SwitchingSet        media type, language, roles, label, channels, flags, ProtectionInfo
      Track             id, bandwidth, codecs, video or audio format, timeline mapping,
                        SegmentIndexProvider, allowed audio sets, typed protocol extension
        SegmentIndex    explicit list or lazy template; Segment values on demand
```

Every node below `Presentation` is held as `shared_ptr<const T>`. A node never changes after `Freeze`, so readers
share it without locks ([ADR-0015](../adr/0015-threading-and-task-runners.md)) and a refresh can reuse a node that
did not change.

| Header | Contents |
|--------|----------|
| `ids.h` | `PeriodId`, `SwitchingSetId`, `TrackId` and the functions that derive them from protocol keys |
| `segment.h` | `Segment`, `ByteRange`, `InitSection`, `KeyReference` |
| `url_template.h` | `UrlTemplate`: the DASH `$Number$`, `$Time$`, `$Bandwidth$` and `$RepresentationID$` identifiers |
| `segment_index.h` | `SegmentIndex`, `ExplicitSegmentIndex`, `TemplateSegmentIndex`, `SegmentIndexProvider`, `MergeSegmentIndexes` |
| `presentation.h` | `Presentation`, `Period`, `SwitchingSet`, `Track`, `ProtectionInfo`, `AdBreak`, `LiveInfo`, `Equivalent` |
| `validate.h` | `ValidateTrack`, `ValidateSwitchingSet`, `ValidatePresentation`, `BuildPresentation` and the size limits |
| `refresh.h` | `RefreshPresentation` |

### Presentation

`type` is `kStatic` or `kDynamic`. `live` is present exactly when the presentation is dynamic. It stores the
availability window, the availability start time, the time-shift depth, the suggested presentation delay, the HLS
hold-back and part hold-back, the minimum update period and the target segment duration. Low-latency segments and
playlist metadata are retained by the HLS parser for later playback support; protection fields are ready for M9.

An HLS multivariant playlist cannot establish whether its presentation is static or dynamic. It starts as dynamic
while its deferred media playlists are unloaded. Once the playlists are loaded, the master is static only when every
loaded media playlist is static: `EXT-X-PLAYLIST-TYPE:VOD` or `EXT-X-ENDLIST` makes a media playlist static; an
`EVENT` or an unfinished playlist keeps it dynamic. A mixture remains dynamic, and `live` is then present.

`generation` counts snapshots. The first snapshot is 0 and each refresh that changes content adds 1.

All times are `MediaTime` and `TimeDelta` from [media types](media-types.md), in presentation time. Parsers
normalise protocol timestamps before they fill the model (D-05), so the model stores no raw ticks except in a
`TemplateSegmentIndex`.

### Periods

Periods are explicit. A DASH `Period` maps one to one. An HLS `EXT-X-DISCONTINUITY` starts a new period, and its
`PeriodId` comes from the discontinuity sequence number. `EXT-X-DATERANGE` interstitials and DASH `EventStream`
ad markers become `AdBreak` values on the presentation, not periods.

`Presentation::FindPeriod(time)` returns the period whose half-open range `[start, start + duration)` holds the
time. A time at a boundary belongs to the later period, and a time at the end of the last period returns nothing. The
last period of a dynamic presentation may have an unknown duration, which makes it open ended.

`AdBreak` carries an id, the start, the duration (or unknown), the resume offset and the playout limit. That is
what ADR-0048 needs as a hook. The primary, presentation and UI timelines and the ad-break table belong to the ad
manager (#248) and map onto periods and these events later.

### Switching sets and tracks

A switching set holds the tracks between which ABR may switch without a visible change of content: one language,
role set and media type. A set has a `StreamType`, a language, `Role` values, a label, a channel configuration,
the `default`, `autoselect` and `forced` flags, and an optional `ProtectionInfo`.

A `Track` has first-class fields for everything that ABR and the decoder choice need:

| Field | Notes |
|-------|-------|
| `bandwidth_bps`, `average_bandwidth_bps` | Combined bandwidth for an HLS variant, as ADR-0032 requires; unknown values remain absent |
| `codecs` | `umbra::codec::CodecDescription` values ([codec configuration](codec-config.md)) |
| `VideoTrackFormat` | Width, height, `frame_rate` as a `Rational`, bit depth, `VideoRange` (SDR, PQ, HLG) |
| `AudioTrackFormat` | Sample rate and a `ChannelConfiguration` with an object-based flag |
| `timeline` | `TimelineMapping`: media time to presentation time ([Timelines](#timelines)) |
| `segments` | A `SegmentIndexProvider` |
| `allowed_audio` | HLS: the audio switching sets that may play with this video track |
| `extension` | `HlsTrackExtension` or `DashTrackExtension`, never a string map |

Dolby Vision is a codec (`DolbyVisionCodec`), not a flag, so `SwitchingSet::CodecIds()` and the codec list reveal it.

Trick-play tracks (HLS I-frame playlists, DASH trick mode) are a separate `Period::trick_play` list. ABR does not
use them.

`ProtectionInfo` holds the encryption scheme (`cenc` or `cbcs`), the DRM system ids, the `InitData` per system, the
key URI and the default key id. It is typed and per switching set, ready for D-27.

### Audio and video coupling

For HLS, each video track lists the audio switching sets that its variant allows in `allowed_audio`. `BANDWIDTH`
stays the combined figure of that variant. For DASH the list is empty and every combination is valid. ABR therefore
picks only among valid combinations. The validator requires that each entry names an audio set of the same period
and that only a video track has entries.

### Track ids

Ids are strings with a type tag, derived from protocol keys and never from URIs, so public track lists survive a
refresh. `EncodeKeyPart` percent-escapes `/` and every byte outside a small safe set, so distinct keys give distinct
ids.

| Id | Derived from |
|----|--------------|
| DASH period | `Period@id`, or the index when it is absent |
| DASH switching set, track | `AdaptationSet@id`, and `Representation@id` |
| HLS period | The discontinuity sequence number |
| HLS video set | The constant `hls/video` |
| HLS variant | `BANDWIDTH`, `RESOLUTION`, `FRAME-RATE`, `VIDEO-RANGE`, `CODECS` and an ordinal that separates duplicates |
| HLS rendition set | Type, `NAME`, `LANGUAGE` and `CHARACTERISTICS`: the same rendition in several groups is one set |
| HLS rendition track | The same, plus `GROUP-ID` |

An HLS variant has no protocol id, so the ordinal is the position among variants with equal other keys. Keep that
rule stable across refreshes.

## Segment indexes

A `Segment` has a sequence number, a presentation `start` and `duration`, a URI, an optional byte range, optional
indexes into the index's `InitSection` and `KeyReference` tables, a discontinuity sequence, a `gap` flag and an
optional program date-time. The sequence number is the identity of the segment: the HLS media sequence number or
the DASH `$Number$`. Sequence numbers are contiguous within an index.

`SegmentIndex` is immutable and has two implementations. Both return segments by value.

HLS `EXT-X-MEDIA` attributes are retained in typed rendition records, including muxed renditions without a URI.
Renditions with media playlists become switching sets and tracks; renditions with the same type, name, language and
characteristics share a switching set across groups. Audio tracks on each variant remain constrained by its
`AUDIO` group. `EXT-X-DATERANGE` retains every attribute by ID, including extension and SCTE-35 values. A refresh
adds new attributes to the prior record; conflicting values fail when strict HLS parsing is enabled and otherwise
use the update value while reporting a manifest warning through the refresh warning sink. A `START-DATE` is mapped onto media time using the nearest
available program-date-time anchor.

| Index | Storage | Use |
|-------|---------|-----|
| `ExplicitSegmentIndex` | A vector of `Segment` | HLS playlists, DASH `SegmentList`, `sidx` |
| `TemplateSegmentIndex` | `UrlTemplate`, a time base and timeline runs | DASH `SegmentTemplate` with `$Number$` or `$Time$` |

A template index stores `TimelineRun` values (`start_ticks`, `duration_ticks`, `count`), so a uniform
representation is one run whatever its length. A representation of 100 000 segments uses the same memory as one of
10, and a test asserts it through `ApproximateBytes()`. URIs are generated in `At(position)`. A `SegmentTimeline`
costs one run per `S` element.

Lookups cost O(log n) for both kinds:

- `FindByTime(time)` returns the position of the segment whose half-open range holds the time. A time before the
  first segment, in a gap between segments, or at or after the end returns nothing.
- `FindBySequence(number)` returns the position of that sequence number, or nothing outside the window.

Tick-to-microsecond conversion rounds down for starts and ends alike, so segments that touch in ticks touch in
microseconds, and each duration is at least one microsecond. `Create` rejects a time base that would break this.

### Provider

A `SegmentIndexProvider` is either resolved (it holds a `SegmentIndex`) or deferred (it holds an `IndexLocation`:
a URI, an optional byte range and the kind of resource). An HLS master playlist produces deferred providers because
the media playlists load later. A DASH `sidx` reference is deferred too. The segment fetcher (#103) resolves a
provider and publishes a new snapshot. A provider can keep its location after resolution. `WithIndex` returns the
resolved copy.

### Timelines

`TimelineMapping` converts between the media timestamps inside the segments and presentation time. It has a media
origin and a presentation origin, and `ToPresentation` and `ToMedia` are exact inverses. The parser sets it:

- DASH: `@presentationTimeOffset` maps to the media origin and the period start to the presentation origin.
- HLS: the first timestamp in a discontinuity sequence maps to the period start.

A `TemplateSegmentIndex` applies the offset itself, because `$Time$` must carry the untouched media ticks while
`Segment::start` is a presentation time. Its parameters are `presentation_time_offset_ticks` and `period_start`. A
parser that fills both a template index and a `TimelineMapping` must keep them consistent.

## Validation and builders

`ValidatePresentation` checks every invariant below and returns `InvalidArgument` with the `ManifestInvalid`
catalogue number (`ALUMBRA-2001`, [error conventions](errors.md)). It treats its input as untrusted: parsers can
produce any shape, and every size has a limit in `validate.h`. A validated presentation has these properties:

- It has one to 65 536 periods, each with a unique non-empty id and a finite, non-negative start. Periods are
  ordered and do not overlap. Each period has a positive finite duration, except that the last period of a dynamic
  presentation may be open ended.
- A static presentation has a finite duration. `live` is present exactly for a dynamic one, with a valid window
  and non-negative durations.
- A period has at least one switching set. Set ids are unique in the period, and track ids are unique across its
  sets and its trick-play list.
- Every track in a set has the set's type. Bandwidth is positive for audio and video. Resolution is both zero or
  both positive, frame rate is positive and at most 1000 times its denominator, and bit depth is 0 or 8 to 16.
- An audio or text track lists no codec of another type. A video track may list audio codecs, because an HLS
  variant lists the codecs of its muxed audio.
- Tracks carry the extension of their protocol only, and a track has a resolved index or an index location.
- Protection entries are unique and bounded. Ad-break ids are unique.

An index validates itself in `Create`: contiguous sequence numbers, finite non-overlapping segments, valid byte
ranges, table indexes in range, no arithmetic overflow, at most 10 million segments, and for a template a
parseable `UrlTemplate` that has `$Number$` or `$Time$`.

`Freeze(Track)`, `Freeze(SwitchingSet)` and `Freeze(Period)` turn a built value into a shared immutable node.
`BuildPresentation` validates a draft and publishes the first snapshot. A parser fills plain structs, freezes them
bottom-up and calls `BuildPresentation`. It needs no setter and no mutable state.

## Live refresh

`RefreshPresentation(previous, next)` takes the snapshot in use and a freshly parsed draft and returns the new
immutable snapshot. It runs on the parsing sequence, so the control sequence only swaps a pointer.

1. Nodes are matched by id: period, switching set, track.
2. For each matched track, `MergeSegmentIndexes` compares the segments that both indexes list. They must have the
   same sequence number, start, duration, URI and byte range, or the refresh fails with `ManifestInvalid`. A window
   that moves backwards or loses segments at its end fails too. Sequence numbers are never renumbered.
3. An explicit index keeps the previous `Segment` for each overlapping sequence number and appends the new ones.
   The init and key tables are rebuilt from the segments that remain, so they do not grow. A template index is
   replaced by the new one after the overlap check, because it stores no segments.
4. A node that equals its predecessor is reused as the same `shared_ptr`. If nothing changed, the call returns
   `previous` itself, so `result == previous` is a cheap "no change" test.
5. A track that is deferred in the draft keeps the index that `previous` loaded for the same location. A refresh of
   only the master playlist does not discard loaded media playlists.
6. Otherwise `generation` is `previous->generation + 1`, and the result is validated.

Changing the protocol, or turning a finished presentation back into a live one, is an error. A live presentation
may become static when the stream ends.

HLS media-playlist reload policy is kept with the parsing-side refresh state. A VOD playlist never reloads, and an
EVENT playlist stops reloading when `EXT-X-ENDLIST` makes it static. After a changed playlist, the next request is
scheduled after the last listed segment's duration; after an unchanged response, it is scheduled after half the
target duration. A playlist unchanged for 1.5 target durations signals an impaired server to the recovery owner
(#127). The policy is maintained per selected media playlist, and only selected rendition URIs are eligible for
requests. `HlsLiveStartPosition` uses a configured end offset when supplied; otherwise it follows `EXT-X-START`
(positive offsets from the window start, negative offsets from its end), `HOLD-BACK`, then three target durations.
Positions are clamped to the current window. URI and byte-range changes for an overlapping sequence number continue
to fail `RefreshPresentation` with `ManifestInvalid` detail.

An explicit index copies its segment vector on each change, which costs O(n) for a window of n segments. Windows
are small next to demuxing, and the unchanged tracks, sets and periods are still shared. For DASH
`SegmentTemplate` the cost is the overlap check only.

To give a template index a stable `$Time$` identity, a parser can call `SequenceForStart(previous_index, start)`.
It returns the sequence number of the previous segment that starts at exactly that time, so a refresh that drops the
first `S` element keeps numbering.

## Equivalence of HLS and DASH models

`Equivalent(a, b)` is the documented equivalence for fixtures that describe the same content in both protocols. It
holds when:

- the presentation type, duration and ad-break starts and durations are equal;
- the periods match in order, with equal start and duration;
- the switching sets of a period match after sorting by type, language, roles, channel count and track count, and
  agree on channels and on the protection scheme and system ids;
- the tracks of a set match after sorting by bandwidth and resolution, and agree on bandwidth, codecs, format and
  on the start and end of every segment.

It ignores the protocol, ids, labels, URIs, sequence numbers, extensions and `generation`. A resolved index only
matches another resolved one.

## Tests

- Unit tests in `test/umbra/manifest/` cover ids, templates, both index kinds, validation, lookup, refresh and the
  equivalence fixtures.
- Property tests use a seeded generator: random timelines compared with an explicit expansion, boundary lookups
  at, just before and just after every segment edge, and 300 random sliding windows that must never renumber.
- `manifest_fuzzer` ([fuzz targets](../../test/fuzz/README.md)) builds presentations from bytes, validates them,
  checks the lookup invariants of valid ones, refreshes them against each other, and exercises the template parser
  and the id encoder.

## References

- [DASH-IF interoperability guidelines](https://dashif.org/guidelines/iop-v5/)
- [HLS specification draft](https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22)
- [Shaka Player `shaka.extern` manifest types](https://shaka-player-demo.appspot.com/docs/api/shaka.extern.html)
