# WebVTT file-parsing deviations

The WPT file-parsing mapping records parser cases that Alumbra can evaluate from decoded UTF-8 input. The deviations
below are deliberate boundaries of the `TextCue` model and are tracked by [#424](https://github.com/LuisAhumadaMartens/alumbra/issues/424).

| WPT behaviour | Alumbra boundary |
|---|---|
| UTF-8 byte-order marks and NUL replacement | Decoding occurs before `WebVttParser` receives text. The parser rejects malformed decoded signatures rather than interpreting transport bytes. |
| Browser CSS stylesheet cascade | The parser retains supported style blocks for Umbra layout. It does not implement DOM stylesheet attachment or computed-style behaviour. |
| Browser `VTTRegion` objects | `TextCue` keeps a region identifier and the parser returns parsed region records. Layout resolves those records without exposing browser object identity or browser-only default-value semantics. |
| Legacy `Region:` header syntax | The current WebVTT model accepts `REGION` blocks only. |
| Browser `auto` cue-setting values | `TextCue` stores resolved numeric layout values and does not expose the DOM's distinct `auto` values. |
