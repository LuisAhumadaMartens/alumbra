# 0008 — Ads: a vendor-neutral AdManager with an IMA provider per platform and a built-in VAST/VMAP provider

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

Google IMA support is a core goal: client-side ads (VAST/VMAP), Dynamic Ad Insertion (SSAI) and Server-Guided Ad
Insertion (SGAI). IMA ships SDKs for HTML5, Android, iOS and tvOS (among others) but **not for C++ or desktop**.
Separately, players commonly suffer from overlay problems: custom controls masking the IMA ad UI (breaking
click-through and skip), or the ad container masking the controls, and viewability measurement failing when
unregistered UI covers the ad.

## Decision drivers

- IMA everywhere it exists; ads still demonstrable on desktop.
- One ad timeline model for CSAI, SSAI and SGAI.
- Correct overlay layering and Open Measurement compliance by construction.

## Considered options

1. **Vendor-neutral AdManager in Umbra + IMA provider per platform + built-in C++ VAST 4/VMAP provider for
   desktop.**
2. IMA only; desktop ships without ads.
3. IMA only; ads start on the web.

## Decision

Chosen option: **1.**

- `umbra::ads` owns ad breaks, cue points (VMAP, SCTE-35, HLS `EXT-X-DATERANGE`/interstitials, DASH events),
  stream-time ↔ content-time mapping, and a registry of UI elements that must be declared as OMID friendly
  obstructions.
- Providers implement a common interface: **IMA HTML5**, **IMA Android**, **IMA iOS/tvOS** (in Penumbra ports),
  and a **built-in VAST 4/VMAP** provider (in Umbra) used on desktop and for tests.
- Stages: client-side VAST/VMAP (M5) → DAI/SSAI and SGAI (M10).
- Overlay masking is solved by an explicit layering and input-ownership contract (D-23).

## Consequences

**Positive**
- Ads work on every platform; IMA specifics are isolated; the timeline model is testable in C++.

**Negative / costs**
- The built-in VAST provider is a second ad implementation to maintain.
- IMA SDK policies apply to each platform integration.

**Follow-ups**
- D-23 layering/input contract and OMID · D-24 fullscreen strategy (incl. iPhone Safari) · D-25 IMA ↔ Umbra
  surface · D-26 DAI/SGAI timeline model.

## References

- [IMA SDKs](https://developers.google.com/interactive-media-ads) ·
  [IMA DAI](https://developers.google.com/ad-manager/dynamic-ad-insertion) ·
  [IAB VAST 4](https://iabtechlab.com/standards/vast/) · [IAB Open Measurement](https://iabtechlab.com/standards/open-measurement-sdk/)
- [research/ios-fullscreen-and-ima.md](../research/ios-fullscreen-and-ima.md) · [research/ssai-and-sgai.md](../research/ssai-and-sgai.md)
