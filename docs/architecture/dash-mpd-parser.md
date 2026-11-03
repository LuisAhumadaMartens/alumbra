# DASH MPD parser

`ParseDashMpd` converts an MPD string into the shared presentation model. It
accepts a caller supplied byte limit, defaults to 16 MiB, and rejects input
larger than that limit. Its XML reader is non-validating and does not resolve
external resources. DTD declarations and entity references other than the five
predefined XML references are rejected. Element nesting is limited to 64 and
the parser limits the document to 200,000 elements.

The parser maps MPD type and duration, live metadata, Period ids, starts and
durations, AdaptationSet ids and media types, Representation ids, bandwidth,
and RFC 6381 codec lists. BaseURL alternatives are resolved through each
hierarchy level in declaration order and retained on the track. SegmentTemplate
and SegmentList become resolved indexes; SegmentBase and SIDX locations remain
deferred. SegmentTimeline runs remain compact, including bounded negative
repeats.

Role, Accessibility, AudioChannelConfiguration, ContentProtection,
EssentialProperty, SupplementalProperty, UTCTiming, Location, PatchLocation,
and EventStream data are mapped into the shared presentation model. Unknown
EssentialProperty schemes exclude the containing AdaptationSet or
Representation and produce a warning. Calendar components in `xs:duration`
are measured from availabilityStartTime. When that attribute is absent, the
Unix epoch is the deterministic calendar anchor for static presentations.
