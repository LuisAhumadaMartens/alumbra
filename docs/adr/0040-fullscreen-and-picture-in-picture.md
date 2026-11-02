# 0040 — Use element or pseudo-fullscreen, and hand off to picture-in-picture by suspending and restoring the UI

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #24

## Context

Fullscreen is where mobile web players most often break: lost UI, hidden ads and wrong orientation. D-24 settles
the API and the mode on each platform. It blocks #155 (M4) and #178 (M5).

iPhone Safari does not implement element fullscreen. The only fullscreen it offers a web page is the native video
presentation of a `<video>` element, which drops every HTML overlay: controls, DOM captions and ad UI. Google also
documents that skippable ads and fullscreen do not work together there. The research is in
[iOS fullscreen and IMA](../research/ios-fullscreen-and-ima.md).

[ADR-0028](0028-headless-ui-state-machine.md) lists fullscreen and picture-in-picture (PiP) as presentation modes
owned by the headless state machine. [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md) makes captions a
separate layer, and [ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md) lets Document PiP serve the
WebCodecs path. The issue ignores desktop fullscreen, which M1 already needs.

D-24 depends on D-23 and D-25, which are M5 decisions. This ADR settles what M4 needs, and those decisions refine
ad behaviour later.

## Decision drivers

- Alumbra's UI, captions and skippable ads keep working in fullscreen wherever possible.
- Capability detection, not device detection.
- PiP must not leave a dead overlay behind, or lose captions.
- The same pattern on every platform, so behaviour is easy to explain and test.
- Ads must never be hidden by a mode change.

## Considered options

iPhone default:

1. **Pseudo-fullscreen**: Alumbra's UI, captions and skippable ads keep working. The system player's controls and
   its fullscreen PiP integration are lost.
2. **Auto**: native fullscreen when no ads and no custom captions are active, pseudo otherwise.
3. **Native by default**: the system player, with IMA and custom UI lost.

## Decision

Chosen option: **pseudo-fullscreen by default on iPhone, plus a PiP handoff.** Elsewhere, element fullscreen where
the browser supports it.

### Fullscreen

- Detect capability with `document.fullscreenEnabled`, not the device.
- API: `requestFullscreen()`, `exitFullscreen()`, a `fullscreenchange` event and a read-only `fullscreenMode`
  (`element`, `pseudo`, `native-video` or `native-app`).
- The headless machine owns the `fullscreen` region. The fullscreen container includes ads and captions. After a
  geometry change, `adsManager.resize` is called.
- **iPhone:** `pseudo`, using a top-layer `<dialog>`, safe-area insets, a scroll lock and a rotate hint.
  `native-video` is opt-in, never during an ad break, and uses captions as native `TextTrack`s.
- **iPad:** element fullscreen through capability detection, pseudo where unsupported. Split View and Slide Over
  behaviour is unverified.
- **Android Chrome:** element fullscreen, with orientation locked to the content aspect ratio and unlocked on exit.
  Ad click-through is left to the SDK, and verified in M5 tests (unverified).
- **Native apps:** iOS uses its own container, reusing the existing layer. Android uses immersive mode on the same
  `SurfaceView`, with no new Activity. TV is always a full window.
- **Desktop:** borderless fullscreen at the desktop mode through SDL3, with no mode switching, so the display refresh
  and ADR-0030's vsync present path are untouched. Compositor direct-scanout behaviour is unverified.
- Guidance for standalone web apps is documentation only.

### Picture-in-picture handoff

- PiP is entered from Alumbra's PiP button, and automatically when the app or tab goes to the background, where the
  platform allows automatic entry. Per-platform support for automatic entry is unverified and #155 confirms it.
- **While in PiP, Alumbra's controls and caption overlay are suspended.** Captions are mirrored to native text tracks
  so they show in the PiP window.
- Everything returns when PiP ends: controls, the caption overlay, and the previous presentation mode, including
  fullscreen.
- **During ad breaks, PiP waits until the break finishes.** A request made during a break is held, then entered when
  the break ends, if the user still wants it.
- The same suspend-and-restore pattern applies to PiP on every platform: the web `<video>` element, Document PiP
  (where the WebCodecs path hosts its canvas), iOS `AVPictureInPictureController`, and Android's PiP mode.
- The headless machine models `pip` as a presentation mode (ADR-0028), with suspend and restore as transitions of the
  UI regions, so each renderer implements the same transitions.

### Settings

`ui.fullscreen.mode` (`auto`, `element`, `pseudo` or `native_video`), `ui.pip.auto_enter` (boolean) and
`ui.pip.mirror_captions` (boolean), per ADR-0017.

## Consequences

**Positive**
- On iPhone, the UI, captions and skippable ads keep working in fullscreen.
- PiP no longer strands controls, and captions stay visible there.
- One pattern describes PiP on every platform.

**Negative / costs**
- iPhone gives up the system player's native controls and its fullscreen PiP integration in the default mode.
- Mirroring captions to native tracks adds a second caption path to keep in step with the overlay (D-15, #15). Styling there is limited to what the platform allows.
- Automatic PiP entry differs between browsers and operating systems, so behaviour will not be identical.
- A deferred PiP during ads can surprise a user who tapped the button.

**Follow-ups**
- #155 builds fullscreen and PiP on the web, including capability detection, the pseudo-fullscreen `<dialog>`, caption
  mirroring and the automatic-entry matrix.
- #178 (M5) applies the ad rules: no PiP entry during breaks, `adsManager.resize`, and click-through checks.
- D-23 (#23) and D-25 (#25) confirm the ad behaviour.
- New task: add the `pip` transitions and suspend and restore to the headless state machine (ADR-0028 follow-up).
- New task: caption mirroring to native `TextTrack`s, driven by `TextCue` (D-15, #140).
- New task: native PiP on iOS (M7) and Android (M6) using the same transitions.
- Add `ui.fullscreen.mode`, `ui.pip.auto_enter` and `ui.pip.mirror_captions` to the settings schema (#155).

## Pros and cons of the options

### Pseudo-fullscreen
- Good, because Alumbra's UI, captions and skippable ads keep working.
- Bad, because the system player's controls and its fullscreen PiP integration are lost.

### Auto
- Good, because it has the best system integration in the plain case.
- Bad, because behaviour changes between sessions, and the UI vanishes mid-roll.

### Native by default
- Good, because it is the system player.
- Bad, because IMA and custom UI are lost.

## References

- [Fullscreen API](https://fullscreen.spec.whatwg.org/)
- [Picture-in-Picture](https://w3c.github.io/picture-in-picture/) and
  [Document Picture-in-Picture](https://wicg.github.io/document-picture-in-picture/)
- [HTML `<dialog>` and the top layer](https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Elements/dialog)
- [Screen Orientation lock](https://developer.mozilla.org/en-US/docs/Web/API/ScreenOrientation/lock)
- [`AVPictureInPictureController`](https://developer.apple.com/documentation/avkit/avpictureinpicturecontroller)
- [Android picture-in-picture](https://developer.android.com/develop/ui/views/picture-in-picture)
- [IMA HTML5 `AdsManager.resize`](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.AdsManager)
- [iOS fullscreen and IMA research note](../research/ios-fullscreen-and-ima.md)
- [ADR-0008](0008-ads-architecture.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0028](0028-headless-ui-state-machine.md), [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md),
  [ADR-0036](0036-web-output-selection-and-ffmpeg-wasm-addon.md)
