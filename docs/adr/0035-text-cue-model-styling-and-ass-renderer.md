# 0035 — Resolve text layout in Umbra, merge user styles field by field and render ASS with Alumbra's own subset renderer

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #15

## Context

One cue model has to cover WebVTT, TTML/IMSC1, SubRip, CEA-608/708, ASS and bitmap subtitles, and decide who lays
out and who paints. D-15 blocks #129, #137 and #140, and shapes caption preferences (#131) and CEA decoding (#136).

Three accepted decisions bound it:

- [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md) draws captions in a separate layer between the video and
  the controls, bundles one OFL font and adds no bidi library in M1. It leaves bidi and line breaking to M3.
- [ADR-0028](0028-headless-ui-state-machine.md) supplies safe-area insets from the UI state machine, so cues can
  avoid the controls.
- [ADR-0014](0014-dependency-and-licensing-policy.md) records libass as needing FriBidi, which is LGPL, and leaves
  ASS unresolved on iOS, web and Android. It denies LGPL in Apple and web artefacts. D-15 had to close that.

## Decision drivers

- One layout implementation to test, with native fonts and accessibility.
- No LGPL in any platform, so ASS behaves the same everywhere.
- Users keep control of caption appearance without losing meaningful author styling, in line with FCC
  expectations.
- Models carry direction and ruby from the start.
- Golden-frame tests (#141) need deterministic output.

## Considered options

Render location:

1. **A — Renderers lay out and style** each format themselves.
2. **B — The platform renders** through native text tracks.
3. **C — Umbra resolves timing, layout and style** into `TextCue` and `BitmapCue`. Renderers only shape and paint.

ASS scope:

1. **Desktop only**, with libass and FriBidi as a reviewed shared library.
2. **Own ASS renderer**: a C++ subset renderer on all platforms.
3. **libass everywhere**, which needs an LGPL path on iOS and web that ADR-0014 does not allow.

User versus author styling:

1. **Field by field.**
2. **Whole override.**
3. **Author wins.**

## Decision

Chosen options: **C for the cue model, an own ASS renderer, and field-by-field styling.**

### Cue model

- Umbra (C++) resolves timing, layout and style into `TextCue` (positioned box, styled runs, direction, ruby) and
  `BitmapCue`. Renderers only shape and paint.
- Layout lives in Umbra: WebVTT cue-box rules, IMSC regions, and 608/708 grids. The 608/708 decoders emit edits, so
  roll-up and paint-on behaviour is preserved.
- Cues avoid the controls through the safe-area insets from ADR-0028, and draw in ADR-0030's caption layer.
- M3 formats: WebVTT, SRT, TTML/IMSC1 text, CEA-608/708 and ASS. PGS, DVB and VobSub follow with Matroska and TS
  sources. `BitmapCue` exists already.

### Own ASS renderer

- ASS and SSA render through Alumbra's own C++ subset renderer on every platform. There is no libass and no LGPL.
- Coverage is a defined supported subset, documented with the feature list. Fidelity for karaoke, transforms and
  drawings is weaker than libass. The subset starts with styles, positioning, colours, basic override tags and
  fades, and grows by test evidence. The exact list is fixed in the issue that implements it.
- Features outside the subset degrade to plain styled text and report a capability warning. They never fail playback.
- This removes ASS from the licence-review path in ADR-0014, and the FriBidi review for ASS is no longer needed.

### Styling precedence

- A field the user has set explicitly, through the system or the app, beats the author's value. Unset fields keep
  author styling. This preserves positioning and speaker colours, and matches FCC expectations and Apple's per-field
  override behaviour.

### Text libraries

- Desktop uses system fonts, with the one bundled OFL fallback. ASS fonts come from Matroska attachments or the
  integrator. Web and mobile fetch no fonts by default.
- Add SheenBidi and libunibreak for bidi and line breaking when text lands in M3, as ADR-0030 anticipated. Both are
  believed permissive; their licences are unverified and need the ADR-0014 review entry.
- A native `TextTrack` mirror is not always on. It exists where D-24 needs it (during PiP, decided in D-24, #24).

## Consequences

**Positive**
- One layout implementation to test, and the same ASS behaviour on every platform.
- No LGPL, so the ASS path needs no replacement procedure.
- User preferences work without discarding author intent.

**Negative / costs**
- Writing and maintaining an ASS renderer is a large effort, and fidelity for karaoke, transforms and drawings is
  weaker. Some existing ASS files will look different from libass output.
- Field-by-field resolution needs more state than a single override.

**Follow-ups**
- #129 builds the cue model and `TextTrack` API.
- #131 implements caption style settings with the field-by-field rule.
- #137 is titled "ASS/SSA via libass". Retitle it to the own subset renderer and define the supported subset in it.
- #136 decodes CEA-608/708 into cue edits.
- #140 implements layout and rendering, and #141 adds golden frames for each format and for ASS subset cases.
- Add SheenBidi and libunibreak to the dependency manifest with licence entries (#38 and #40 process).
- Add `text.*` settings per ADR-0017, such as `text.font_size_percent`, `text.background_opacity_percent` and
  `text.ass.enabled`, each with an "explicitly set" state for the precedence rule (#131).
- Update `docs/compatibility.md` and the roadmap to drop libass, done with this ADR.

## Pros and cons of the options

### Umbra-resolved layout (C)
- Good, because there is one implementation to test.
- Bad, because Umbra owns more code.

### Own ASS renderer
- Good, because it is uniform on every platform and carries no LGPL.
- Bad, because it is a large effort with weaker fidelity at first.

### libass on desktop only
- Good, because it is the fastest route and licence-clean on desktop.
- Bad, because web, iOS and Android would have no ASS support.

### libass everywhere
- Good, because fidelity is the best.
- Bad, because it fails ADR-0014 on Apple and web, and no FriBidi-free build exists (a patched fork is unverified).

### Field-by-field styling
- Good, because users win where they chose and authors keep the rest.
- Bad, because resolution state is more complex.

## References

- [WebVTT](https://www.w3.org/TR/webvtt1/)
- [IMSC1.1 text profile](https://www.w3.org/TR/ttml-imsc1.1/)
- [ASS (Advanced SubStation Alpha) format description](https://github.com/libass/libass/wiki/ASS-File-Format-Guide)
- [libass](https://github.com/libass/libass) and [FriBidi](https://github.com/fribidi/fribidi), for the dependency not used
- [SheenBidi](https://github.com/Tehreer/SheenBidi) and [libunibreak](https://github.com/adah1972/libunibreak)
- [FCC rules for closed captioning of IP-delivered video, 47 CFR 79.103](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-C/part-79/subpart-A/section-79.103)
- [ADR-0014](0014-dependency-and-licensing-policy.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0028](0028-headless-ui-state-machine.md), [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md)
