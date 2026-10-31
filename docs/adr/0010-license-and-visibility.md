# 0010 — Public from day one under Apache-2.0, with an LGPL/permissive-only dependency policy

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

The planning trail is part of the deliverable, so the repository is public from the first commit. Media players
depend on libraries with varied licences (FFmpeg is LGPL-2.1+ by default but GPL when built with components such
as x264/x265; libass is ISC; dav1d is BSD-2-Clause; libsrt is MPL-2.0). Apple's App Store terms complicate
LGPL static linking.

## Considered options

1. **Public, Apache-2.0.**
2. Public, MIT.
3. Private until the first milestone, then Apache-2.0.

## Decision

Chosen option: **1 — public, Apache-2.0** (the same licence as Cobalt; its explicit patent grant is valuable for
media code).

Dependency policy:
- Allowed: permissive licences (BSD, MIT, ISC, Apache-2.0, zlib) and weak copyleft (LGPL, MPL-2.0) **only when
  dynamically linked or otherwise compliant** for the target platform.
- Forbidden: GPL/AGPL components (e.g. FFmpeg built with `--enable-gpl`, x264, x265).
- Each third-party dependency is listed with its licence in `THIRD_PARTY.md` (created with the first dependency).

## Consequences

- Clear reuse terms for integrators.
- Some FFmpeg features are unavailable; mobile ports rely on platform decoders rather than FFmpeg.

**Follow-ups**
- D-32 dependency and licensing policy details (FFmpeg configure flags, App Store implications).
