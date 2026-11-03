# Segment fetching

`umbra::fetch` (`umbra/fetch/`, namespace `umbra::fetch`) decides what each active stream downloads next and issues
every request through the HTTP transport of the [port contract](port-contract.md). It follows the split of Shaka's
`StreamingEngine` and Media3's `LoadControl`: a policy says whether to load, and the loaders decide how. The module
is engine-internal. Its headers are in `umbra/fetch/internal_include/umbra/fetch/` and are not installed, and it
includes no platform header. It depends on `umbra::base`, `umbra::manifest`, `umbra::source` (URL resolution only)
and the Penumbra interface, so it builds and tests against the fake transport and needs no libcurl.

Retries, backoff and failover belong to the error ladder of [ADR-0018](../adr/0018-error-model-and-recovery.md)
(#127). The fetcher reports a failure and stops that stream until `Retry`, `Seek` or a new track selection.

## Shape

| Header | Contents |
|--------|----------|
| `fetch_types.h` | `FetchConfig`, `LoaderState`, `LoadWindow` and `LoadPolicy`, `SegmentSink`, `TransferTimingSink`, `IndexResolver` |
| `init_cache.h` | `InitCache`: least recently used map keyed by URL and byte range |
| `segment_fetcher.h` | `SegmentFetcher`, `StreamSelection`, `FetcherDependencies` |

One `SegmentFetcher` owns a loader per `StreamKind` (video and audio; text joins in M3). Every method must run on the
control sequence ([ADR-0015](../adr/0015-threading-and-task-runners.md)). The transport may call back from any
thread, so the callbacks only post a task to the control sequence, and nothing blocks. A callback for a request that
was cancelled finds no request and is dropped.

## Loader states

| State | Meaning |
|-------|---------|
| `kIdle` | Nothing queued: the buffer goal is reached, or the stream ended, or no track is selected |
| `kLoadingIndex` | A deferred index (HLS media playlist, DASH `sidx`) is being fetched |
| `kLoadingInit` | The oldest undelivered item is an initialisation segment |
| `kLoadingMedia` | The oldest undelivered item is a media segment |
| `kWaitingForAvailability` | A dynamic presentation has no segment past the frontier yet |
| `kError` | A request failed; the sink has been told. `Retry` or `Seek` resumes |

## What a loader queues

Each stream keeps an ordered queue of items: an index, an init segment or a media segment. Planning runs whenever
something changes (a completion, `Seek`, `SelectTrack`, `UpdateTrack`, `OnBufferChanged`):

1. The frontier is the presentation time at which loading continues. After a seek it is the target. Afterwards it is
   the end of the last queued segment. `LocateNext` finds the segment that holds it, the first one after a gap, or
   the first one when the frontier lies before the window. Segments flagged as gaps are skipped. Choosing by time,
   not sequence number, keeps an ABR switch correct when the tracks number their segments differently.
2. The `LoadPolicy` returns a `LoadWindow` for the stream. Planning stops at the first segment that starts at or
   after `prefetch_until`, and when the estimated bytes of the queue would exceed `byte_budget`. The estimate is the
   byte range length if there is one, otherwise `bandwidth * duration`. The first queued segment is always allowed,
   which keeps the goal at no less than one segment ([ADR-0034](../adr/0034-buffer-model-and-memory-budgets.md)).
   The buffer manager (#124) implements `LoadPolicy` and calls `OnBufferChanged` when the buffer drains.
3. A segment whose `init_index` names a section that differs from the last queued one is preceded by an init item.
   This handles an HLS `EXT-X-MAP` change and a track switch with the same code. A cached init segment is queued as
   already complete.
4. At most `max_queued_segments_per_stream` media items wait in a queue.

Segment, init and index URLs are resolved against `StreamSelection::base_url`, which the caller sets to the final
URL of the manifest after redirects (RFC 3986 section 5). A byte range becomes a `Range` request and must be
answered with 206 and exactly that many bytes. A body of the wrong size is `DataLoss`. Other statuses map to
`PermissionDenied`, `NotFound`, `DeadlineExceeded`, `Unavailable` or `InvalidArgument`.

## Request classes and caps

The class of a media item is decided when it is dispatched, not when it is queued. A segment that starts before
`demand_until` is `kMedia`. A later one inside the goal is `kPrefetch` and becomes `kMedia` once the playhead
approaches. An index is `kManifest` and an init segment is `kInit`. Dispatch orders candidates by class (manifest,
init, media, prefetch), then by segment start time, then by stream. A request starts only when all of these hold:

| Limit | Default | Range |
|-------|---------|-------|
| Total requests in flight | 6 | 1 to 16 |
| Requests per host (authority of the URL) | 6 | 1 to 8 |
| Per class: manifest, init, media, prefetch | 2, 2, 4, 2 | at least 1 |

A request that is blocked by a cap stays queued while a lower-ranked candidate of another class may start. Priority
hints follow the class: prefetch is `kLow`, media `kNormal`, the rest `kHigh`. `ValidateFetchConfig` checks the
ranges. The defaults are starting values. The settings keys `streaming.fetch.max_concurrent_requests`,
`streaming.fetch.max_concurrent_requests_per_host` and `streaming.init_cache.max_entries` map onto `FetchConfig`
when the player wires the engine; the fetcher takes a plain struct and reads no settings itself.

## Cancel on supersede

- `Seek(position)` bumps the generation, cancels every request of every stream, discards partial and completed but
  undelivered data, and sets the frontier to the target. It returns the new generation. The sink sees only items
  of that generation afterwards, and a late callback finds no request. So a segment requested before a seek is never
  appended after it.
- `SelectTrack(kind, selection, mode)` with `kCancelInFlight` is the emergency down-switch: all requests of the
  stream are cancelled and loading restarts at the last delivered time on the new track. With `kAfterInFlight` the
  stream keeps requests that already started and drops the queued ones. Their data is delivered first, tagged with
  the old track, then the new track's init segment and media follow, so the sink always sees a consistent order.
- `ClearTrack` cancels and deselects.

Segments are delivered strictly in order per stream, even when concurrent requests finish out of order.

## Initialisation segment cache

The cache is keyed by URL and byte range, holds `init_cache_max_entries` entries (default 16, range 1 to 256) and
evicts the least recently used. An init segment is fetched once per representation. After a seek, or a switch back to
a track, the init segment is delivered again from the cache with `from_cache` set, because a demuxer that was
reset needs it. A cached delivery makes no request and no timing sample.

## Deferred indexes and live

A track whose provider is deferred makes the loader fetch the index through `IndexResolver::ParseIndex`, which the
HLS (#106) and DASH (#113) parsers implement. The loader keeps the resolved provider (`WithIndex`) and reports it
through `SegmentSink::OnIndexResolved`, so the owner can publish a new immutable snapshot with
`RefreshPresentation`. The fetcher never mutates a presentation.

For a dynamic presentation the owner calls `UpdateTrack` with the refreshed track after each manifest refresh. The
loader waits in `kWaitingForAvailability` while no segment is listed past the frontier and continues on the update. A
frontier that fell behind the window jumps to its first segment. Scheduling the refreshes is the parser's job. The
stream reports `OnEndOfStream` only for a static presentation.

## Hand-off to the demuxer

`SegmentSink` is the narrow interface. `OnInitSegment` and `OnMediaSegment` carry the track id, the generation, the
`Segment` (or `InitSection`) and the bytes as `SharedBytes`. The ISO-BMFF demuxer ([demuxers](demuxers.md)) takes
bytes through `Demuxer::Append(offset, bytes)` and `Reset`. An adapter that implements `SegmentSink` appends the init
segment and then each media segment at the running offset, and calls `Reset` when the generation changes. The fetcher
does not depend on `umbra::demux`.

## Timing for ABR

Every completed init, media or prefetch request produces one `TransferSample` for the `TransferTimingSink`: the
request class, the body bytes, the time to first byte (response start minus request start), the transfer time
(completion minus response start), the coarse-timing flag and the arrival time of each chunk
([ADR-0026](../adr/0026-network-stack-and-http-transport.md), [ADR-0033](../adr/0033-abr-algorithm-and-bandwidth-estimation.md)).
Cancelled and failed requests give no sample. The bandwidth estimator (#119) is not merged at the time of writing.
Its interface takes `AddSample(bytes, ttfb, transfer_time, cache_hit)` and
`AddChunkSample(bytes, ttfb, active_transfer_time, cache_hit)`, so an adapter that implements
`TransferTimingSink` forwards one or the other, and ignores the request classes it does not want. The `chunks` list is
there for the active-transfer computation.

## Tests

`test/umbra/fetch/` runs the fetcher on `TestTaskEnvironment` (the fake clock and a deterministic sequence) and the
scripted `FakeHttpTransport`. The tests compare the request log with golden sequences for VOD, seeks and switches,
and cover the concurrency caps (the maximum observed in flight), the init cache, cancel on seek, byte ranges, error
mapping, goals and budgets, deferred indexes and live waiting. Integration tests against the in-tree HTTP server join
once the libcurl transport (#75) is in the tree.

## References

- [Shaka Player `StreamingEngine`](https://github.com/shaka-project/shaka-player/blob/main/lib/media/streaming_engine.js)
- [Media3 `LoadControl`](https://developer.android.com/reference/androidx/media3/exoplayer/LoadControl)
- [RFC 3986 section 5: Reference resolution](https://www.rfc-editor.org/rfc/rfc3986#section-5)
