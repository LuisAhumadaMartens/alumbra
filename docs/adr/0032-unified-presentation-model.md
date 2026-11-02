# 0032 — Parse HLS and DASH into one presentation model with explicit periods

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #10

## Context

HLS and DASH must parse into one model that ABR, buffering, live refresh, track lists and ad timelines all consume.
D-10 blocks #102 and #103. A wrong model means either duplicated ABR and live logic, or lost protocol detail that a
later feature needs.

The neighbouring decisions already constrain it:

- [ADR-0015](0015-threading-and-task-runners.md) forbids blocking the control sequence. A live manifest refresh must
  therefore parse off that sequence and hand over a finished result.
- [ADR-0016](0016-penumbra-port-contract.md) keeps fMP4 pass-through behind the model, not in it.
- [ADR-0023](0023-public-api-surface-and-media-error.md) fixes the public track lists, which need stable track ids
  across refreshes.
- D-26 (#26) needs exact ad-break boundaries, and D-27 (#27) needs DRM signalling per switching set.
- D-05 normalises timestamps in the parsers, so the model does not.

## Decision drivers

- ABR, buffering and live logic written once, not once per protocol.
- Protocol detail stays typed, never a string map, so no feature has to re-parse.
- An exact location for ad breaks, codec changes and key rotation.
- Lock-free readers, in line with ADR-0015.
- Frame rate, codec, resolution, bit depth and HDR as first-class fields, so ABR can respect decoder and display
  refresh rate, including above 60 fps.

## Considered options

The issue's options for the model:

1. **A — Two models**, one per protocol, with ABR and buffering written against both.
2. **B — A common denominator**, with protocol details in string maps.
3. **C — A unified core with typed protocol extensions.**

Period handling:

1. **Explicit periods**: HLS discontinuities and DASH periods are boundaries in the model.
2. **Flatten periods**: a single timeline with boundaries as events, as Shaka does.

## Decision

Chosen options: **C, with explicit periods.**

### Model

- The core hierarchy is `Presentation → Period → SwitchingSet → Track`, with a lazy `SegmentIndexProvider` per track.
  Protocol extensions are typed structs attached to these nodes.
- A track carries frame rate, codec, resolution, bit depth and HDR flags as fields.
- Trick-play (I-frame playlists, DASH trick mode) is a separate list per period. ABR does not use it. Thumbnails are
  deferred.
- DRM signalling is a typed `ProtectionInfo` (scheme, system ids, init data, key URI) per switching set, handed to
  D-27.

### Periods

- HLS `EXT-X-DISCONTINUITY` becomes a period boundary. DASH `Period` maps one to one.
- `EXT-X-DATERANGE` interstitials become ad-break events (D-26), not periods.
- ABR and buffer code handle boundaries. This is the cost of the choice, and the accepted one.

### Audio and video coupling

- HLS: each video track lists its allowed audio switching sets. `BANDWIDTH` stays the combined figure for that
  variant.
- DASH: every combination is valid.
- ABR therefore chooses among valid combinations only.

### Live refresh and track identity

- A refresh parses off the control sequence and publishes an immutable `shared_ptr<const Presentation>` snapshot to
  it, sharing unchanged segment lists with the previous one.
- Track ids derive from protocol keys, never from URIs: DASH `AdaptationSet` and `Representation` ids, and HLS
  `GROUP-ID`, `NAME`, `LANGUAGE` and `CHARACTERISTICS`. Public track lists (D-03) then survive a refresh.

## Consequences

**Positive**
- ABR, buffer and live logic exist once.
- Ad breaks, codec changes and key rotation have an exact place in the model.
- Readers take no locks, and a refresh costs little next to demuxing.

**Negative / costs**
- Every consumer must handle period boundaries.
- Typed extensions mean new protocol features need a model change, not just a parser change.
- The HLS coupling lists add model surface that DASH does not use.

**Follow-ups**
- #102 implements the types, with the snapshot and stable-id rules above.
- #103 builds the segment fetcher on `SegmentIndexProvider`.
- #119, #120 and #124 consume the model through D-11 and D-12 ([ADR-0033](0033-abr-algorithm-and-bandwidth-estimation.md),
  [ADR-0034](0034-buffer-model-and-memory-budgets.md)).
- #110 and #115 map discontinuities, `DATERANGE` and DASH periods onto the boundaries.
- D-26 (#26) and D-27 (#27) read the period boundaries and `ProtectionInfo`.

## Pros and cons of the options

### A — Two models
- Good, because each protocol's detail is exact.
- Bad, because ABR, buffering and live logic are duplicated and drift.

### B — A common denominator
- Good, because it is the smallest model.
- Bad, because protocol detail ends up in untyped maps and every later feature re-parses it.

### C — A unified core with typed extensions
- Good, because the logic is written once and the detail stays typed.
- Bad, because the model is larger and needs an owner for each extension.

### Flatten periods
- Good, because ABR is simpler.
- Bad, because codec changes and ad-break timing are harder to reconstruct, and D-26 needs the boundaries.

## References

- [RFC 8216: HTTP Live Streaming](https://www.rfc-editor.org/rfc/rfc8216)
- [HLS authoring specification for Apple devices](https://developer.apple.com/documentation/http-live-streaming/hls-authoring-specification-for-apple-devices)
- [ISO/IEC 23009-1: MPEG-DASH](https://www.iso.org/standard/79329.html)
- [DASH-IF interoperability guidelines](https://dashif.org/guidelines/)
- [Shaka Player](https://github.com/shaka-project/shaka-player), the flattened-period prior art
- [ADR-0015](0015-threading-and-task-runners.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0023](0023-public-api-surface-and-media-error.md)
