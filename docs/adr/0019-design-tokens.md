# 0019 — Generate design tokens from pure DTCG sources with wide-gamut and HDR support

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #29

## Context

[ADR-0007](0007-ui-architecture.md) makes W3C Design Tokens Community Group (DTCG) tokens the single source of
styling. The headless state machine (D-28, #28) decides what the player shows; tokens decide how it looks on every
renderer: SDL3 on desktop (D-19, #19), CSS on the web, Compose on Android and SwiftUI on Apple platforms. The
playground edits tokens live, and integrators re-skin the player with them, so tokens are public API and naming
mistakes are expensive to undo.

DTCG published its first stable specification, 2025.10, as three modules: Format (values, types, groups, aliases,
`$deprecated`, `$extensions`), Color (colour spaces including Display P3 and Rec. 2020) and Resolver (combining
token sets with modifiers such as theme and density). The Color module defines no HDR colour space.

## Decision drivers

- Tokens are public API: stable names, explicit stability levels and a deprecation path.
- One source generates CSS and TypeScript, Swift, Kotlin and C++ outputs that cannot drift.
- Modes expressed the standard way, so tools stay replaceable.
- The UI must look right on wide-gamut displays and over HDR video, not only on sRGB SDR screens.
- No tool or account in the critical path of an open-source project.

## Considered options

1. **A — Style Dictionary v4 with custom formats.**
2. **B — Terrazzo.**
3. **C — Alumbra's own generator**, sharing the settings generator's infrastructure
   ([ADR-0017](0017-settings-schema-and-configuration.md)).

## Decision

Chosen option: **C.** ADR-0017 already gives Alumbra a stdlib-only Python generator with committed outputs and a
drift check. Swift, Compose and C++ outputs would be custom code under any tool, and the generator implements the
Resolver module exactly instead of emulating modes with multiple builds. Because the source stays pure DTCG, with
no tool-specific syntax, Style Dictionary or Terrazzo can replace the generator later.

### Source and outputs

- Tokens are pure DTCG 2025.10 JSON under `design/tokens/`.
- The generator resolves aliases and modes and emits CSS custom properties and TypeScript constants, Swift
  (SwiftUI), Kotlin (Compose) and C++ `constexpr` tables. Generated files are committed and checked for drift.

### Taxonomy

- Three tiers: **primitive** (`color.neutral.900`, `space.4`), **semantic** (`color.surface.overlay`,
  `motion.duration.short`) and **component** (`player.seekbar.played`, `player.captions.window`).
- Renderers reference only semantic and component tokens, so a theme is a remapping of aliases.
- Semantic and component tokens are public API. Each component token declares a stability level (`stable` or
  `experimental`), as settings do.

### Naming

- Dotted `lower_snake` segments, matching settings keys; singular nouns (`color`, not `colors`).
- An interaction state is the last segment: `hover`, `focus`, `pressed` or `disabled`, as in
  `player.button.background.hover`.
- Generated CSS names follow mechanically: `--alumbra-player-button-background-hover`.

### Modes

Modes are expressed with the DTCG Resolver module:

- **theme:** dark (default) and light;
- **contrast:** normal (default) and high;
- **density:** desktop in M0 and M1; mobile and TV arrive with their platforms (web in M4, Google TV in M6);
- **motion:** full (default) and reduced;
- **dynamic range:** SDR (default) and HDR, described below.

On the web the defaults follow `prefers-color-scheme`, `prefers-contrast` and `prefers-reduced-motion` unless the
app sets a mode explicitly. The dynamic-range mode follows what is being presented, not only the display: it is
HDR only while HDR video is shown on an HDR-capable output, because the `dynamic-range` media feature reports
capability, so SDR playback on an HDR screen keeps the SDR values.

### Wide gamut and HDR

- **Wide gamut.** Colours may be authored in sRGB, Display P3 or Rec. 2020 through the Color module. For every
  colour outside sRGB the generator also emits a gamut-mapped sRGB fallback, and each platform output uses the
  wide-gamut form only where both the display and the platform support it. On the web, a wide-gamut override sits
  behind the `color-gamut` media feature and an `@supports` check for the colour syntax, so an older browser on a
  wide-gamut display keeps the sRGB value instead of an invalid one.
- **HDR.** The Color module has no HDR colour space, so HDR uses standard number tokens and the dynamic-range mode.
  A UI reference-white luminance, defaulting to 203 cd/m² (ITU-R BT.2408 graphics white), sets how bright UI
  white appears when composited over HDR video, so controls and captions do not glare or look dim. In the HDR
  mode, tokens may add headroom above reference white for deliberate highlights. Renderers map these values to
  each platform's HDR compositing facilities and fall back to the SDR values elsewhere; the platform mechanics
  belong to D-19 (#19) and the HDR work of M11. If DTCG adds HDR colour spaces, the source adopts them.

### Runtime themes

Integrators can override semantic and component tokens at runtime through one typed token-map API on every
platform. The generator emits the alias dependencies with the resolved defaults, so overriding a semantic token
re-resolves every component token that aliases it, while a component token the integrator overrode explicitly
keeps that value. The web, Kotlin and Swift SDKs load JSON themes with their platforms' JSON support. Loading a
theme file in the C++ desktop app waits until a JSON library passes the dependency review of
[ADR-0014](0014-dependency-and-licensing-policy.md). The playground exports themes in this format.

### Typography

Font families are tokenised as platform font stacks: SF on Apple platforms, Roboto on Android and `system-ui` on
the web. SDL3 has no system font stack, so the desktop renderer bundles one OFL-licensed font, which ADR-0014
allows for fonts.

### Workflow

The repository is the only source of truth. Figma import or export through DTCG JSON may be added later, but no
design tool is required to contribute.

### Deprecation and lint

- A renamed token keeps a `$deprecated` alias to its replacement for at least one minor release, as settings do.
  During that period the runtime token map and the CSS override surface also accept the deprecated name and apply
  it to the replacement; if a theme sets both, the replacement wins.
- CI rejects renderer references to primitive tokens, raw colour or dimension values in renderer code, tokens
  without a `$description`, and names that break the rules above.

## Consequences

**Positive**
- One pure DTCG source styles every renderer, and its outputs cannot drift.
- Integrators re-skin the player at runtime without rebuilding, through one API on every platform.
- The UI uses wide-gamut colour where displays support it and stays legible over HDR video.
- The generator can be replaced by a standard tool without touching the source.

**Negative / costs**
- Alumbra owns compliance with the parts of DTCG it uses, including the Resolver module.
- Component tokens are public, so renaming them follows the deprecation policy.
- HDR UI support relies on Alumbra's convention for luminance tokens until DTCG defines HDR colour.
- The desktop renderer ships a bundled font.

**Follow-ups**
- #58 defines the taxonomy and the base token set, including the modes and the HDR luminance tokens.
- #59 generates the CSS, TypeScript, Swift, Kotlin and C++ outputs with sRGB fallbacks.
- #60 adds the token validation, lint rules and drift check, and checks #58's contrast thresholds on the generated
  sRGB fallbacks as well as the authored colours, in every mode and against each token's declared backdrop, since
  gamut mapping can lower contrast.
- D-19 (#19) and M11 map wide-gamut and HDR tokens onto each renderer's compositing.
- D-28 (#28) defines the state machine whose states the component tokens style.

## References

- [D-29 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/29)
- [DTCG: Design Tokens Format Module 2025.10](https://www.designtokens.org/tr/2025.10/format/)
- [DTCG: Design Tokens Resolver Module 2025.10](https://www.designtokens.org/tr/2025.10/resolver/)
- [DTCG: Design Tokens Color Module 2025.10](https://www.designtokens.org/tr/2025.10/color/)
- [Recommendation ITU-R BT.2408: Guidance for operational practices in HDR television production](https://www.itu.int/rec/R-REC-BT.2408)
- [CSS Color Module Level 4](https://www.w3.org/TR/css-color-4/)
- [Media Queries Level 5: `dynamic-range`](https://www.w3.org/TR/mediaqueries-5/#dynamic-range)
- [Style Dictionary: DTCG support](https://styledictionary.com/info/dtcg/)
- [Terrazzo documentation](https://terrazzo.app/docs/)
- [Alumbra ADR-0007: UI architecture](0007-ui-architecture.md)
