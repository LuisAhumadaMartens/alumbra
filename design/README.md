# Design — the single source of truth for the player's look

`tokens/` holds the player's design tokens in [W3C DTCG](https://www.designtokens.org/) JSON: colour, typography,
spacing, radii, motion, focus rings, caption styles and ad-break chrome. A build step generates CSS custom
properties, Swift, Kotlin and C++ from them, so every renderer looks the same and a design change is one edit.

`playground/` (milestone M4) is a live web page for editing tokens and seeing the player update instantly.

Taxonomy and pipeline are open definition **D-29**. See [ADR-0007](../docs/adr/0007-ui-architecture.md).
