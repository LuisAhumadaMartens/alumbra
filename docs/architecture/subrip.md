# SubRip parsing

SubRip sources enter through `TextTrack::LoadSubRip`. The parser accepts cues with or without numeric indices,
out-of-sequence indices, comma or full-stop millisecond separators, CRLF, LF or CR endings, overlapping cue times,
blank lines between blocks, and text after the final cue. Invalid cue blocks are skipped. A 16 MiB input bound
limits retained parser work.

The parser converts `<i>`, `<b>`, `<u>` and `<font>` markup into the cue's styled text runs, including font colour.
It decodes the common SubRip HTML entities and maps `\\an1` through `\\an9` to cue positions. Other markup is
removed while its text remains.

A UTF-8, UTF-16LE or UTF-16BE byte order mark selects that encoding. Without a byte order mark, valid UTF-8 is
accepted. The current fallback converter supports Windows-1252 and ISO-8859-1. `text.srt.fallback_encoding` and
`text.charset.auto_detect` provide the parser settings; callers pass their effective values to `LoadSubRip`.
The parser exposes the selected encoding and confidence through the track and logs both fields in the `text`
category. `SetEncodingOverride` re-decodes the retained source bytes and replaces the cues without loading the
media again.

Statistical detection and conversion for ISO-8859 variants other than ISO-8859-1, Shift_JIS, GB18030 and other
legacy encodings remain unavailable until an encoding conversion dependency has a version-specific licence
review under [ADR-0014](../adr/0014-dependency-and-licensing-policy.md). A detector alone cannot convert those
encodings into UTF-8.
