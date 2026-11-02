# 0006 — Bring up platforms in the order Desktop → Web → Android/Google TV → Apple

- **Status:** Accepted; platform scope superseded in part by [ADR-0031](0031-windows-support-scope-and-toolchain.md)
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

All platforms are in scope: desktop (macOS, Linux), Web, Android & Google TV, iOS, iPadOS & tvOS. The first
vertical slice determines how quickly the engine becomes real, testable and demoable.

## Decision drivers

- Fast iteration and debugging for the engine itself.
- Headless, cheap CI.
- Validate the riskiest design (hybrid web output) early.
- Living-room relevance (Google TV) before Apple.

## Considered options

1. **Desktop → Web → Android/Google TV → iOS/iPadOS/tvOS.**
2. Web → Desktop → Android/Google TV → Apple.
3. Desktop → Android/Google TV → Web → Apple.

## Decision

Chosen option: **1.** Desktop (macOS + Linux) is the **reference port**, comparable to Cobalt's Linux reference
platform: FFmpeg/dav1d software decode, an SDL3 window, and headless runs in CI. Web follows, then Android &
Google TV (MediaCodec, D-pad), then iOS/iPadOS/tvOS (VideoToolbox, FairPlay, native IMA).

## Consequences

**Positive**
- The engine is proven on the friendliest platform before any constrained one.

**Negative / costs**
- IMA (no desktop SDK) arrives with the Web port; desktop uses the built-in VAST/VMAP provider (ADR-0008).
