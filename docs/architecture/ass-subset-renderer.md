# ASS and SSA subset renderer

Umbra parses ASS and SSA into positioned `TextCue` values. It does not use libass and does not rasterise glyphs. The shared caption renderer shapes and paints the resulting runs.

## Supported subset

The initial subset accepts `[Script Info]` `PlayResX` and `PlayResY`, `[V4+ Styles]` style names, font family, font size, primary colour, bold, italic, underline and alignment, plus `[Events]` dialogue timing and text. SSA `[V4 Styles]` uses the same supported style fields. The parser maps `\pos`, `\an`, `\b`, `\i`, `\u`, `\c`, `\1c`, `\r`, `\fad`, `\N`, `\n` and `\h` to cue position, run style, fade timing or text.

Matroska `S_TEXT/ASS` supplies the script header as codec private data and event fields in each block. `AssParser::ParseMatroskaEvent` combines those inputs and uses the block's sample interval as dialogue timing. Embedded-font extraction and font registration are not part of this parser API; the caption renderer receives the requested font family and integrator-provided font resources through its own font provider.

Karaoke tags, transforms, vector drawings, movement, secondary and outline styling, margins, and effects outside the subset are not interpreted. The parser retains visible event text as styled text, strips override blocks, and returns a capability warning. These warnings let playback continue while exposing the fidelity loss.

Input is bounded by script, event-count and event-size limits. A malformed event is skipped with a warning. Invalid or missing play resolution uses the ASS default of 384 by 288. Cue positions and sizes are percentages of the script resolution; run font size remains in script layout units for the renderer to scale.

Golden frame coverage is owned by the shared caption renderer tests (#141), where each subset case can be compared through the common layout and drawing path.
