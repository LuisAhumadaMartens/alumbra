# 0005 — Support DRM in stages: ClearKey/CENC first, then platform DRM

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

DRM is central to commercial streaming and to the HTML5 media contract (EME). Commercial CDMs (Widevine,
FairPlay, PlayReady) are closed: a C++ engine cannot decrypt their content itself. It can only route encrypted
samples, initialisation data and licence messages to the platform: EME on the web, `MediaDrm` on Android,
`AVContentKeySession` (FairPlay) on Apple platforms.

## Decision drivers

- Demonstrate the full EME architecture (key systems, sessions, encrypted sample metadata).
- Avoid licensing friction early; never bypass platform DRM rules.

## Considered options

1. **Staged:** ClearKey/CENC first, then Widevine/FairPlay/PlayReady through each platform's native path.
2. ClearKey/CENC only.
3. Out of scope; only design the CDM interface.

## Decision

Chosen option: **1.** Umbra implements an EME-shaped model (`MediaKeys`, `MediaKeySession`, init data, key
status) and CENC sample metadata for both `cenc` and `cbcs`. ClearKey is implemented end-to-end first; platform
DRM follows in milestone M9 through Penumbra.

## Consequences

**Positive**
- The DRM architecture is exercised and tested early with no third-party credentials.

**Negative / costs**
- FairPlay testing requires Apple FPS credentials; Widevine L1 behaviour can only be tested on real devices.

**Follow-ups**
- D-27 CDM abstraction and encryption-scheme handling.
