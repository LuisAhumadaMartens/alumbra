# TTML parser

The parser accepts a bounded XML document and returns timed text cues for the player text-track model. Its input
limit defaults to 4 MiB, its XML nesting limit defaults to 64 elements, its
tree is limited to 100,000 nodes, and it emits at most 10,000 cues. Callers
can lower or raise those limits through `TtmlParserOptions`; `text.ttml.max_document_bytes` is the configured
document-size setting.

DTD declarations and entity declarations are rejected before XML parsing. Only XML's five predefined entities
are decoded. External resources are never opened, and general entity expansion is unavailable. Invalid markup,
unsupported entities, invalid timing expressions and limit violations return an `absl::Status` carrying the
structured media-corrupt error detail.

The parser resolves inherited begin times and end bounds, explicit `end` and `dur` values, clock and offset times,
frame-rate multipliers, tick rates and `timeBase="media"`. A sample caller supplies its media-time offset through
`sample_time_offset`, so every sample's cue times are mapped onto the containing media timeline.

Text paragraphs become `TextCue` values and inline spans become `StyledTextRun` values. The current player cue model
retains basic bold, italic, underline, language, direction, writing orientation, ruby text, region identity and cue
origin. The model does not yet represent the complete IMSC style tree or decode embedded PNG images. DASH `stpp`
sample extraction and conformance-stream presentation are not connected to this parser yet.

The parser has unit tests for offset timing, frame and tick rates, sample offsets, document and depth limits, and
external-entity and billion-laughs rejection. `ttml_parser_fuzzer` runs arbitrary byte inputs through the same
limits and is registered for corpus replay and libFuzzer builds.
