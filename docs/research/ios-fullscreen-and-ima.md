# iOS fullscreen and IMA

Last verified: 2026-10-31

On iPhone, Safari still does not implement the element Fullscreen API. The only fullscreen available to a web page is
the native video presentation started from a `<video>` element, and it hides every HTML overlay: controls,
subtitles rendered in the DOM, and ad UI. This note documents those constraints and how the IMA HTML5 SDK behaves
under them. It also covers what changes in a native app, and why custom controls over ads must be declared to Open
Measurement. It ends with the decisions Alumbra needs to make for D-23 and D-24.

## 1. Element fullscreen on iPhone vs iPad

| Capability | iPhone (Safari iOS) | iPad (Safari iPadOS) | macOS Safari |
|------------|---------------------|----------------------|--------------|
| `Element.requestFullscreen()` | Not available | Unprefixed since 16.4; `webkit`-prefixed since 12 (partial) | Unprefixed since 16.4 |
| `<video>.webkitEnterFullscreen()` | Yes (native player UI) | Yes | Yes |
| HTML overlays in fullscreen | Dropped (native presentation) | Kept with element fullscreen | Kept with element fullscreen |
| `ScreenOrientation.lock()` | Not supported | Not supported | Not supported |

Sources:

- **Compatibility data.** MDN's compatibility data for
  [`Element.requestFullscreen`](https://developer.mozilla.org/en-US/docs/Web/API/Element/requestFullscreen)
  ([BCD source](https://github.com/mdn/browser-compat-data/blob/main/api/Element.json)) marks Safari iOS 16.4+
  (unprefixed) and 12+ (`webkit` prefix) as partial, with these notes:
  - "Only available on iPad, not on iPhone."
  - "Shows an overlay button which can not be disabled. Swiping down exits fullscreen mode."
- **WebKit's announcement.** [Safari 16.4](https://webkit.org/blog/13966/webkit-features-in-safari-16-4/) shipped the
  unprefixed Fullscreen API "on macOS and iPadOS".
- **Open WebKit bug.** [Bug 206854, "Add Fullscreen API to iOS"](https://bugs.webkit.org/show_bug.cgi?id=206854) is
  still in status NEW.
- **Recent Safari release notes.** These change details, not the iPhone limitation:
  - [Safari 26.4](https://developer.apple.com/documentation/safari-release-notes/safari-26_4-release-notes) fixed
    `Element.requestFullscreen` on iOS so it reflects hardware-keyboard state.
  - [Safari 27](https://developer.apple.com/documentation/safari-release-notes/safari-27-release-notes) "Fixed
    subtitles and closed captions not appearing in fullscreen video on iOS."
- **Native video fullscreen.** On iPhone the only path is
  [`HTMLVideoElement.webkitEnterFullscreen()`](https://developer.apple.com/documentation/webkitjs/htmlvideoelement/1633500-webkitenterfullscreen).
  - Related members: `webkitSupportsFullscreen`, `webkitDisplayingFullscreen`, `webkitExitFullscreen()`, and the
    events `webkitbeginfullscreen` / `webkitendfullscreen`.
  - The video is handed to the system player, so DOM content layered over the `<video>` is not shown.
- **Orientation.** MDN data shows
  [`ScreenOrientation.lock()`](https://developer.mozilla.org/en-US/docs/Web/API/ScreenOrientation/lock) as
  unsupported in Safari, including iOS, so a page cannot force landscape.

## 2. Why an iframe does not help

Wrapping the player in an `<iframe>` changes the security boundary, not the capability:

- The `allowfullscreen` attribute (legacy form of `allow="fullscreen *"`) only lets the framed document "activate
  fullscreen mode by calling the `requestFullscreen()` method"
  ([MDN `<iframe>`](https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Elements/iframe)). On iPhone that
  method does not exist for any element, framed or not.
- Inside the iframe the only option is still `webkitEnterFullscreen()` on a `<video>`, which drops overlays in exactly
  the same way.
- An iframe can isolate an ad or a verification script (IMA's OMID `CREATIVE` mode requires sandboxing the player in a
  secure iframe, see section 7). It cannot make HTML controls visible over native fullscreen video.

## 3. Inline playback: `playsinline`

- Since iOS 10, `<video playsinline>` plays inline on iPhone instead of automatically entering fullscreen
  ([WebKit, "New `<video>` Policies for iOS"](https://webkit.org/blog/6784/new-video-policies-for-ios/)).
- Autoplay rules from the same post:
  - Muted videos may autoplay.
  - Unmuted playback requires a user gesture. The qualifying events are `touchend`, `click`, `doubleclick` and
    `keydown`.
  - `webkit-playsinline` is the legacy attribute.
- In `WKWebView`, inline playback and gesture requirements are controlled by
  [`allowsInlineMediaPlayback`](https://developer.apple.com/documentation/webkit/wkwebviewconfiguration/allowsinlinemediaplayback)
  and `mediaTypesRequiringUserActionForPlayback`.
- Alumbra's web port must always set `playsinline`. Otherwise the first `play()` on iPhone jumps into native
  fullscreen, and Antumbra's UI and the ad container vanish.

## 4. IMA HTML5 SDK on iOS

The client-side IMA SDK plays ads "in a separate video player positioned on top of the app's content video player"
([IMA HTML5 get started](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side)).

### 4.1 Setup requirements

- **The ad container must sit on top.** The `AdDisplayContainer` container element "should be correctly positioned,
  ideally above and covering the content video player"
  ([`AdDisplayContainer` reference](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.AdDisplayContainer)).
  - The sample positions `#adContainer` absolutely over the video.
  - `initialize()` "must be called as a direct result of a user action".
  - Resizing uses `adsManager.resize(width, height, google.ima.ViewMode.NORMAL | FULLSCREEN)`.
- **Click tracking on iPhone.** The optional third constructor argument, `clickTrackingElement`, exists because some
  environments "do not support UI elements overlaying a video player (for example, iPhone…)". The SDK uses it only
  for non-AdSense/AdX creatives.

### 4.2 Custom playback

On iPhone the SDK's default is *custom playback*: it plays ads in the content `<video>` element itself.
[`ImaSdkSettings.setDisableCustomPlaybackForIOS10Plus`](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.ImaSdkSettings)
describes the switch:

> If true, ads will play inline if the content video is inline. This enables TrueView skippable ads. However, the ad
> will stay inline and not support iOS's native fullscreen. When false, ads will play in the same player as your
> content. You must set this value before creating the AdDisplayContainer. The default value is false.

The [skippable-ads guide](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/skippable-ads)
adds the following:

- Skippable ads require inline content (`playsinline`) and custom playback disabled on iPhone.
- "Custom playback is always disabled on Android Chrome."
- Disabling it "breaks fullscreen implementations that rely on `videoPlayer.requestFullscreen()` on iPhone".
- "The IMA SDK does not currently support fullscreen on iPhone with skippable ads."

| Mode (iPhone) | Ad player | Native fullscreen during ad | Skip button / overlay UI | Clicks |
|---------------|-----------|-----------------------------|--------------------------|--------|
| Custom playback on (default) | Content `<video>` | Possible (ad plays in the same element) | Not available as HTML; no TrueView skippable | `clickTrackingElement` path |
| Custom playback off | SDK-owned `<video>` in the ad container | Not supported | Rendered by the SDK in the ad container | Ad container must be topmost |

Consequence: on iPhone a web player cannot offer native fullscreen and IMA skippable ads at the same time. A CSS
pseudo-fullscreen (section 5) is the only way to look fullscreen while keeping the IMA ad UI (skip button, "Learn
more", countdown) interactive.

## 5. CSS pseudo-fullscreen

Pseudo-fullscreen keeps the player inline but makes its container fill the viewport:

| Technique | Purpose | Reference |
|-----------|---------|-----------|
| `position: fixed; inset: 0;` and a high `z-index` on the player root | Cover the page | CSS |
| `height: 100dvh` (with `100vh` fallback) | Track the dynamic viewport as Safari's toolbars collapse or expand | [Safari 15.4 viewport units](https://webkit.org/blog/12445/new-webkit-features-in-safari-15-4/) |
| `<meta name="viewport" content="..., viewport-fit=cover">` plus `env(safe-area-inset-*)` padding | Use the full display while keeping controls out of the notch and home indicator | [Designing Websites for iPhone X](https://webkit.org/blog/7929/designing-websites-for-iphone-x/) |
| Lock page scroll (`overflow: hidden` on the root) while active | Prevent the page scrolling behind the player | (unverified best practice) |
| Call `adsManager.resize(w, h, google.ima.ViewMode.FULLSCREEN)` on enter/exit and on `resize` | Keep the IMA layout in sync | IMA get started |
| Show a "rotate device" hint instead of locking orientation | `screen.orientation.lock()` is unavailable | MDN |

Limits:

- Safari's browser chrome may remain partly visible, depending on scroll and toolbar state (unverified, varies by iOS
  version).
- The status bar is not hidden.
- It is not a real fullscreen for accessibility tools or AirPlay.

A hybrid is common in practice (unverified as a documented recommendation):

- Use pseudo-fullscreen while an ad is playing or while Alumbra overlays are needed.
- Offer `webkitEnterFullscreen()` for content-only playback, accepting native controls and native caption rendering
  there.

## 6. Native app path: IMA iOS SDK

In a native iOS app, Alumbra owns the view hierarchy, so the web restrictions disappear:

- **Setup.** IMA iOS client-side uses `IMAAdDisplayContainer` (initialized with an ad container `UIView` and a view
  controller), `IMAAdsLoader`, `IMAAdsRequest` and `IMAAdsManager`. Content progress is reported through
  `IMAAVPlayerContentPlayhead` or a custom playhead
  ([IMA iOS get started](https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side)).
- **Layout.** Ads again play "in a separate video player positioned on top of the app's content video player". The
  app keeps that container view topmost in its own fullscreen presentation.
- **Fullscreen.** Fullscreen is just a layout of Alumbra's own view or view controller. Controls, subtitles and the ad
  container can all coexist, because nothing is handed to a system player unless Alumbra chooses `AVPlayerViewController`.

## 7. Open Measurement and friendly obstructions

The IAB Open Measurement SDK (OM SDK) measures viewability. Any view drawn over the ad counts as an obstruction
unless it is registered as *friendly*:

- **What OM SDK does.** In OM SDK, `AdSession.addFriendlyObstruction(view, purpose, detailedReason)` marks a view that
  is "excluded from all ad session viewability calculations"
  ([OM SDK Android reference](https://docs.iabtechlab.com/omsdk-1.5/android/index-all.html)). The purposes are:
  - `VIDEO_CONTROLS`: "relates to interacting with a video (such as play/pause buttons)";
  - `CLOSE_AD`;
  - `NOT_VISIBLE`: "not visibly obstructing the ad but may seem so due to technical limitations";
  - `OTHER`.
- **IMA iOS** (requires IMA iOS 3.9.0+;
  [OMSDK guide](https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/omsdk)):
  - API (Objective-C): `[[IMAFriendlyObstruction alloc] initWithView:purpose:detailedReason:]`, then
    `[adDisplayContainer registerFriendlyObstruction:]` and `unregisterAllFriendlyObstructions`.
  - [`IMAFriendlyObstructionPurpose`](https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/reference/Enums/IMAFriendlyObstructionPurpose)
    has the values `MediaControls` (0), `CloseAd` (1), `NotVisible` (2) and `Other` (3).
  - Google's guidance: do not cover the ad container. Register transparent overlays and transient controls. Do not
    register watermarks, dialogs or non-transient buttons.
  - `detailedReason` must be 50 characters or fewer and use only letters, digits and spaces.
- **IMA Android** ([OMSDK guide](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/omsdk)):
  - API: `ImaSdkFactory.createFriendlyObstruction(view, purpose, reason)`, then
    `AdDisplayContainer.registerFriendlyObstruction()` and `unregisterAllFriendlyObstructions()`.
  - The
    [`FriendlyObstructionPurpose`](https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/api/reference/com/google/ads/interactivemedia/v3/api/FriendlyObstructionPurpose)
    values are `VIDEO_CONTROLS`, `CLOSE_AD`, `NOT_VISIBLE` and `OTHER`.
- **IMA HTML5.**
  - The [OMSDK guide](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/omsdk) says the
    SDK includes OM SDK v1.6 and reads `<AdVerifications>`.
  - Access modes are `FULL`, `CREATIVE` and `LIMITED`, set per vendor through `AdsRequest.omidAccessModeRules`.
    `DOMAIN` now falls back to `LIMITED`, and `LIMITED` is the default.
  - The [`google.ima` namespace reference](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima)
    exposes **no friendly-obstruction API** (checked 2026-10-31). On the web, the only safe rule is not to draw
    Alumbra UI over the ad container while an ad plays.

Why it matters: custom Antumbra controls, subtitle layers or gesture-capturing transparent views over an ad lower its
measured viewability, which can affect monetization. On native platforms each such view must be registered with the
correct purpose for the duration of the ad. On the web they must be hidden or moved outside the ad's rectangle.

## 8. Decisions to make

**D-23: layering and input-ownership contract**

1. **Z-order per platform.** Fix the bottom-to-top order (video surface, subtitles, Antumbra controls, ad container)
   and decide whether any Antumbra element may ever sit above the ad container.
2. **Input ownership.** While an ad is active, who receives taps and keys? Choose between IMA owning the ad rectangle
   with Antumbra's control bar hidden or limited, and Antumbra keeping only a mute/fullscreen toggle outside the ad
   rectangle.
3. **Obstruction registry in Umbra.** Every Antumbra view declares a role: media controls, close ad, not visible,
   other, or "never over ads". Each port maps roles to `registerFriendlyObstruction` (iOS, Android) or to
   hide/relocate behaviour (web, no API).
4. **Subtitles during ads.** Hide them, or keep them and register them with the "not visible" or "other" purpose.
5. **iPhone clicks.** When to supply `clickTrackingElement` in custom-playback mode.

**D-24: fullscreen strategy**

1. **iPhone web.** Default to CSS pseudo-fullscreen whenever ads or Alumbra overlays are needed. Decide whether to
   offer `webkitEnterFullscreen()` for content-only playback, which hands off to native controls and caption
   rendering.
2. **IMA mode on iPhone.** Choose between custom playback disabled (skippable ads, no native fullscreen) and enabled
   (native fullscreen, no skippable ads). The recommendation to evaluate is disabled, combined with pseudo-fullscreen.
3. **iPad and desktop web.** Use `Element.requestFullscreen()` on the player root so overlays survive, and detect
   support through `document.fullscreenEnabled`.
4. **Native apps (iOS, tvOS, Android).** Fullscreen is an Antumbra layout of Alumbra's own views, with no system
   player. Decide whether `AVPlayerViewController` is ever used, for example for picture-in-picture or AirPlay UI.
5. **Orientation.** Handle rotation as a user action on the web (no lock available) and through app orientation APIs
   on native platforms.

## References

- MDN. *Element.requestFullscreen()*. https://developer.mozilla.org/en-US/docs/Web/API/Element/requestFullscreen
- MDN browser-compat-data. *api/Element.json*. https://github.com/mdn/browser-compat-data/blob/main/api/Element.json
- MDN. *ScreenOrientation.lock()*. https://developer.mozilla.org/en-US/docs/Web/API/ScreenOrientation/lock
- MDN. *`<iframe>` element*. https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Elements/iframe
- WebKit. *WebKit Features in Safari 16.4*. https://webkit.org/blog/13966/webkit-features-in-safari-16-4/
- WebKit Bugzilla. *Bug 206854: Add Fullscreen API to iOS*. https://bugs.webkit.org/show_bug.cgi?id=206854
- WebKit. *New `<video>` Policies for iOS*. https://webkit.org/blog/6784/new-video-policies-for-ios/
- WebKit. *New WebKit Features in Safari 15.4*. https://webkit.org/blog/12445/new-webkit-features-in-safari-15-4/
- WebKit. *Designing Websites for iPhone X*. https://webkit.org/blog/7929/designing-websites-for-iphone-x/
- Apple. *Safari 26.4 Release Notes*. https://developer.apple.com/documentation/safari-release-notes/safari-26_4-release-notes
- Apple. *Safari 27 Release Notes*. https://developer.apple.com/documentation/safari-release-notes/safari-27-release-notes
- Apple. *webkitEnterFullscreen()*. https://developer.apple.com/documentation/webkitjs/htmlvideoelement/1633500-webkitenterfullscreen
- Apple. *WKWebViewConfiguration.allowsInlineMediaPlayback*. https://developer.apple.com/documentation/webkit/wkwebviewconfiguration/allowsinlinemediaplayback
- Google. *IMA HTML5: Get started*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side
- Google. *IMA HTML5: AdDisplayContainer*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.AdDisplayContainer
- Google. *IMA HTML5: ImaSdkSettings*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima.ImaSdkSettings
- Google. *IMA HTML5: Enable skippable ads on mobile*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/skippable-ads
- Google. *IMA HTML5: google.ima namespace*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/reference/js/google.ima
- Google. *IMA HTML5: Open Measurement SDK*. https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/omsdk
- Google. *IMA iOS: Get started*. https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side
- Google. *IMA iOS: Open Measurement SDK*. https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/omsdk
- Google. *IMA iOS: IMAFriendlyObstructionPurpose*. https://developers.google.com/interactive-media-ads/docs/sdks/ios/client-side/reference/Enums/IMAFriendlyObstructionPurpose
- Google. *IMA Android: Open Measurement SDK*. https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/omsdk
- Google. *IMA Android: FriendlyObstructionPurpose*. https://developers.google.com/interactive-media-ads/docs/sdks/android/client-side/api/reference/com/google/ads/interactivemedia/v3/api/FriendlyObstructionPurpose
- IAB Tech Lab. *OM SDK 1.5 Android API index*. https://docs.iabtechlab.com/omsdk-1.5/android/index-all.html
