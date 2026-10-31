# 0007 — UI: design tokens + headless UI state machine in C++ + thin native renderers

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens

## Context

The player needs a styled UI on every platform, including fullscreen and during ad breaks, and a **single place
of truth for design** that can be iterated quickly. Platform realities differ: accessibility services, TV focus
engines (D-pad, Siri Remote), native fullscreen, and ad SDKs that insert their own views.

## Decision drivers

- One source of truth for appearance; one source of truth for behaviour.
- Native accessibility, focus and fullscreen behaviour.
- Ad SDK containers (IMA) must coexist with our controls.

## Considered options

1. **Design tokens + shared headless UI state machine in C++ + thin native renderer per platform.**
2. One UI drawn by the C++ engine everywhere (Skia/RmlUi), pixel-identical.
3. One HTML/CSS web-components UI hosted in a WebView on native platforms.

## Decision

Chosen option: **1.**

- **Appearance:** W3C Design Tokens (DTCG) JSON in `design/tokens/` is the single source of truth, generated into
  CSS custom properties, Swift, Kotlin and C++, and previewed in a live design playground.
- **Behaviour:** `umbra::ui` is a headless state machine (controls visibility, menus, ad mode, focus graph for
  D-pad, accessibility labels) shared by all platforms.
- **Drawing:** thin renderers — Web Components, Jetpack Compose, SwiftUI, and a desktop renderer — that render
  the state and forward input.

## Consequences

**Positive**
- Native accessibility and fullscreen; consistent behaviour; design iterated in one file.

**Negative / costs**
- Four renderers to maintain; visual parity is enforced with screenshot tests.

**Follow-ups**
- D-19 desktop renderer · D-28 state machine design · D-29 token taxonomy and pipeline.
