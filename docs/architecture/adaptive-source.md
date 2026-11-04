# Adaptive source

`umbra::pipeline::AdaptiveSource` adapts an immutable HLS or DASH presentation
to the pipeline's `SampleSource` interface. Manifest probing and parsing happen
before construction; the source receives the parsed presentation, its final
manifest URL, the port's HTTP transport and the task runner.

The source selects the first video switching set in the first available
period. For audio it applies the player's configured preferred-language list
using RFC 4647 basic filtering and truncation, then prefers the manifest's
default, autoselect or main role, and otherwise takes the first playable set.
An enabled audio-description preference selects a description role before
language matching. It passes the selected tracks to `SegmentFetcher`, which resolves
relative segment URLs, fetches initialisation and media segments, observes byte
budgets and keeps delivery ordered. Its default load policy uses the supplied
buffer forward goal and audio/video byte limits; an embedder may supply a
`LoadPolicy` that also accounts for current port budgets and buffered samples.
Static presentations report their declared duration. Dynamic presentations
report an infinite duration.

The source can select another track from the active switching set. The fetcher
finishes requests already in flight, then fetches the new initialisation
segment before its media segment. Decoder configuration changes are carried on
the first affected sample.

For a dynamic DASH presentation, the source resolves UTC timing before it
starts the segment fetcher. It then refreshes the MPD at its minimum update
period, applies stable presentation snapshots to the selected streams and
stops refreshing when an update changes the presentation to static. See the
[live DASH architecture](dash-live.md) for timing and refresh rules.

The source holds one demuxer per selected stream. MPEG-TS segments use the TS
demuxer, while ISO-BMFF initialisation and media segments use the MP4 demuxer.
The initialisation segment establishes stream configuration; each media segment
is reset to offset zero, parsed and converted to encoded samples. Stream ids are
mapped to the pipeline's video and audio ids. The presentation's timeline
mapping is applied to sample timestamps.

Seeking uses each selected track's segment index. Accurate mode keeps the
requested decode position so the playback state machine can discard frames
until the target. Fast mode starts decoding at the preceding segment boundary,
or at the nearest boundary when requested.
When adjacent segments overlap in presentation time, video samples before the
previous segment end are discarded and audio samples are trimmed at that
boundary. A discontinuity starts a new overlap range.

`test/umbra/pipeline/adaptive_source_test.cc` covers accurate and fast segment
lookup, live duration, and a scripted HLS TS segment fetched through the fake
transport and delivered through `SampleSink`.
