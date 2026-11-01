# Design — the single source of truth for the player's look

`tokens/` holds the player's design tokens in [W3C DTCG](https://www.designtokens.org/) JSON: colour, typography,
spacing, radii, motion, focus rings, caption styles and ad-break chrome. A build step generates CSS custom
properties, Swift, Kotlin and C++ from them, so every renderer looks the same and a design change is one edit.

`playground/` (milestone M4) is a live web page for editing tokens and seeing the player update instantly.

The taxonomy and pipeline are defined by [ADR-0019](../docs/adr/0019-design-tokens.md), under the UI architecture
in [ADR-0007](../docs/adr/0007-ui-architecture.md). Proposed implementation details for names, generated output
identifiers, modes and runtime themes are listed in the
[settings and token implementation contract](../docs/architecture/adr-0017-0019-implementation-contract.md) and
remain subject to review in [#58](https://github.com/LuisAhumadaMartens/alumbra/issues/58),
[#59](https://github.com/LuisAhumadaMartens/alumbra/issues/59) and
[#60](https://github.com/LuisAhumadaMartens/alumbra/issues/60).
