# 0042 — Keep player chrome off the ad and reduce it to an ad band during ad breaks

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #23

## Context

D-23 decides which z-order, input ownership and obstruction rules guarantee that Alumbra's UI never masks the ad
SDK's interactive UI, and the reverse. [ADR-0008](0008-ads-architecture.md) requires an explicit contract, and
[ADR-0028](0028-headless-ui-state-machine.md) provides the place to enforce it: its ad region already knows when an
ad break is active, who owns input (D-23 is named there) and which controls may be activated. The contract is
visible in every ad break, so a mistake costs clicks, skips and viewability.

Two failure modes are well known from web players. Player chrome above the ad container swallows clicks meant for
click-through, skip or "Why this ad". An ad container that stays above the player after the break swallows clicks
meant for the player. Open Measurement (OMID) also counts anything drawn over an ad as an obstruction, which lowers
viewability unless the SDK is told it is a friendly obstruction.

Platform facts, as recorded in the definition issue:

- **IMA HTML5** has no friendly-obstruction API. It embeds the OM SDK and takes access modes through
  `AdsRequest.omidAccessModeRules`. On the web the only safe contract is that no Alumbra UI is drawn over the ad
  container while an ad plays.
- **IMA Android** registers obstructions with `ImaSdkFactory.createFriendlyObstruction` and
  `AdDisplayContainer.registerFriendlyObstruction`. **IMA iOS and tvOS** (3.9.0 and later) use
  `IMAFriendlyObstruction`, whose `detailedReason` is at most 50 characters of letters, digits and spaces.
- **TV** needs D-pad focus on the skip button. IMA HTML5 offers `adsManager.focus()` and IMA Android offers
  `AdsRenderingSettings.setFocusSkipButtonWhenAvailable`.
- **Desktop** has no IMA SDK. The built-in VAST provider draws its own ad UI, so Alumbra owns both sides. Whether an
  OM SDK exists for native desktop is unverified; it is out of scope here.

[ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md) already draws captions in a layer separate from the
controls, which this contract reuses. The roadmap places this definition in M5, which builds web and desktop;
native obstruction registration belongs to M6 and M7.

## Decision drivers

- Alumbra UI never masks an ad, and the ad never masks Alumbra's controls once it ends.
- Viewers keep basic control during an ad, such as pause and mute, instead of depending on each SDK's own controls.
- One contract on every platform, with a per-platform geometry rule rather than per-platform behaviour.
- The contract is enforced by the headless state machine and checked by automated hit tests.
- TV focus reaches the skip button first and never leaves an ad by accident.

## Considered options

1. **A — Ad container on top, chrome fully hidden during ads.** Simplest, but the viewer cannot pause or mute
   without the SDK's own controls.
2. **B — Ad container on top, plus a reduced ad band that never overlaps it.** The viewer keeps basic controls;
   it costs layout work and a smaller ad area.
3. **C — Player chrome above the container with pointer pass-through.** Viable on native with registration, but
   it breaks the "nothing over the container" rule on the web and fails OMID measurement there.

## Decision

Chosen option: **B**, with the controls and geometry below. It keeps viewer control without drawing anything over
an ad, and it is the only rule that holds on the web, where obstructions cannot be declared.

### Z-order and input ownership

- From bottom to top: video surface; text tracks; content chrome (hidden in ad mode); ad container (the IMA
  container, or the built-in provider's UI on desktop); ad band; system overlays such as picture-in-picture and
  native fullscreen.
- Ad mode exposes `inputOwner = ad | band | none` from the state machine, derived from geometry and focus.
  Alumbra never handles pointer events inside the ad rectangle.
- The ViewModel already makes hidden controls inert, so a control that ad mode hides cannot be activated by input
  or through the API.

### Controls in ad mode

- Allowed in the band: pause, volume and mute, fullscreen, the ad badge and the countdown.
- Hidden: the seek bar, captions, settings and playback speed.
- Captions are hidden during ads by default. If shown, they are registered as `OTHER` on native platforms and
  placed outside the ad rectangle on the web.

### Geometry

- **Web.** The ad container shrinks to leave a band below it, and the provider is told through
  `adsManager.resize`. The band never overlaps the ad. In pseudo-fullscreen (D-24, #24) the band stays and is
  sized to the safe areas.
- **Native (M6 and M7).** The band is an overlay and is registered as a friendly obstruction while visible, with
  purpose `VIDEO_CONTROLS` or `MediaControls`. Only transient controls are registered, as Google advises against
  registering watermarks, dialogs and other non-transient elements.
- **Desktop.** The built-in VAST provider follows the same contract. OMID is out of scope on desktop.

### TV focus

Focus starts on the skip button. A "down" press moves focus to the band, and "back" moves focus to the band and
never exits an ad silently.

### Registry

Umbra's AdManager owns the registry of obstruction purposes and `detailedReason` strings. Providers consume it, so
each platform registers the same elements the same way.

### Tests

- **Web.** Playwright hit tests call `document.elementsFromPoint` over a grid inside the ad rectangle and assert
  that the top element belongs to the ad container, and that band targets belong to Alumbra.
- **Android and Apple.** Espresso and XCUITest equivalents arrive with M6 and M7.

## Consequences

**Positive**
- Ad clicks, skips and viewability are protected by geometry, not by hoping the SDK copes with overlays.
- Viewers can pause, mute and leave fullscreen during an ad on every platform.
- The rule is testable with golden ViewModels and hit tests, and the same code serves desktop.

**Negative / costs**
- The web ad area is smaller than the player by the band's height.
- Layout work for the band, in pseudo-fullscreen and on small screens.
- Two per-platform geometry rules (off-rectangle on the web, registered overlay on native) must be kept
  consistent.

**Follow-ups**
- #164 adds ad mode to the UI state machine with `inputOwner`, the allowed controls and the focus rules.
- #165 builds the obstruction registry, owned by Umbra's AdManager.
- #175 implements the web geometry, `adsManager.resize` handling and the `elementsFromPoint` regression tests.
- #177 implements the layering and input-ownership contract in the renderers.
- Android and Apple obstruction registration and tests are part of the M6 (#196) and M7 (#214) IMA providers.
- The band's tokens and states follow ADR-0019; its string labels follow the localisation set.

## Pros and cons of the options

### A — Hide all chrome
- Good, because nothing can mask the ad and no OMID declarations are needed.
- Bad, because viewers cannot pause or mute without the SDK's own controls, which vary by format and platform
  (unverified per SDK), and iPhone's native controls may be the only ones.

### B — Reduced ad band
- Good, because viewers keep basic control, OMID viewability is clean on the web and the rule is the same on every
  platform.
- Bad, because the band takes space and needs layout work.

### C — Chrome above the container
- Good, because the viewer keeps the full chrome and native registration handles viewability.
- Bad, because on the web it fails OMID measurement with no way to declare the obstruction, it is fragile against
  IMA layout changes and arbitration on TV is hard.

## References

- [IMA HTML5: Open Measurement SDK](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/omsdk)
- [IMA Android: Open Measurement SDK](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/omsdk)
- [IMA iOS: Open Measurement SDK](https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/omsdk)
- [IMA Android: AdsRenderingSettings](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/api/reference/com/google/ads/interactivemedia/v3/api/AdsRenderingSettings)
- [IMA HTML5: AdsManager reference](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.AdsManager)
- [IAB Tech Lab: Open Measurement SDK](https://iabtechlab.com/standards/open-measurement-sdk/)
- [Media3: AdOverlayInfo](https://developer.android.com/reference/androidx/media3/common/AdOverlayInfo)
- [MDN: Document.elementsFromPoint()](https://developer.mozilla.org/en-US/docs/Web/API/Document/elementsFromPoint)
- [ADR-0008](0008-ads-architecture.md), [ADR-0028](0028-headless-ui-state-machine.md),
  [ADR-0030](0030-desktop-graphics-audio-and-ui-renderer.md)
- [Research: iOS fullscreen and IMA](../research/ios-fullscreen-and-ima.md)
