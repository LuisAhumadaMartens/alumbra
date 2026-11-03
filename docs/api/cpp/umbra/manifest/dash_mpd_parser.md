# DASH MPD parser

`umbra::manifest::ParseDashMpd` parses a bounded MPD document into the
presentation model. It returns `ManifestInvalid` for malformed input, unsupported
XML entities, invalid timeline data, and values outside the model's time range.
The default input limit is 16 MiB. The non-validating XML reader does not fetch
external resources and limits nesting to 64 elements and total elements to
200,000.

Calendar months and years in `xs:duration` are evaluated against
`availabilityStartTime`. If an MPD omits that attribute, the Unix epoch is used
as the deterministic anchor for calendar duration conversion.
