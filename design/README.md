# Design tokens

`tokens/` is the source of player design data in the stable DTCG 2025.10
[Format](https://www.designtokens.org/TR/2025.10/format/),
[Color](https://www.designtokens.org/TR/2025.10/color/) and
[Resolver](https://www.designtokens.org/TR/2025.10/resolver/) modules. The sources are portable JSON; CSS,
TypeScript, Swift, Kotlin and C++ outputs belong to #59 and are not generated or activated by this change.
`playground/` is planned for M4.

## Taxonomy

| Tier | Source | Purpose | Reference rule |
|------|--------|---------|----------------|
| Primitive | `primitives.tokens.json` | Raw palette, scales and platform-neutral values | May hold literals or aliases |
| Semantic | `semantic.tokens.json` | Role-based decisions such as `color.surface.base` and `motion.duration.short` | Must alias a primitive; only a `$deprecated` alias to a same-tier replacement is permitted |
| Component | `components.tokens.json` | Player-owned UI contracts such as `player.button.background` | Aliases semantic or component tokens; declares `stable` or `experimental` |

Semantic tokens require `$description`. Component descriptions and stability live in the vendor-namespaced
`$extensions.com.alumbra` object. Caption-style tokens are reserved under `player.captions` for M3; ad chrome is
reserved under `player.ad_chrome` for M5. Those namespaces are experimental until their renderer milestones.
`player.ad_band.background` and `player.ad.band.background` are intentionally distinct public paths.
Every token in primitive, semantic, component and context source files has a non-empty `$description`, as
required by ADR-0019's deprecation and lint contract.

Names use dotted paths of lowercase ASCII letters, digits and single internal underscores. Each segment starts
with a letter or is numeric; dots and hyphens separate path components. The chosen output mappings are:

- CSS: `--alumbra-` plus path segments joined with hyphens; underscores within a segment stay unchanged.
  `player.ad_band.background` maps to `--alumbra-player-ad_band-background`, while
  `player.ad.band.background` maps to `--alumbra-player-ad-band-background`.
- Flat SDK identifiers: prefix `alumbra_token_`, encode each source underscore as `_u`, and join path segments
  with `_s`. The same paths map to `alumbra_token_player_sad_uband_sbackground` and
  `alumbra_token_player_sad_sband_sbackground`.

The resting value beside interaction states uses DTCG's `$root` token. Its DTCG reference path retains the
`$root` segment; the public output mapping treats `$root` as structural and maps it to the enclosing group path.
These mapping choices follow the companion contract selected for #58 and are recorded for owner review in the
[issue discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/58#issuecomment-6554797534); they do not
amend accepted ADRs.

## Contexts and rollout

`resolver.json` follows the DTCG Resolver 2025.10 module. Its orthogonal modifiers and defaults are:

| Modifier | Contexts | Default |
|-----------|----------|---------|
| `theme` | `dark`, `light` | `dark` |
| `contrast` | `normal`, `high` | `normal` |
| `density` | `phone`, `tablet`, `desktop`, `tv` | `desktop` |
| `motion` | `full`, `reduced` | `full` |
| `dynamic-range` | `sdr`, `hdr` | `sdr` |

All four density datasets are authored with context-specific type, spacing and minimum targets
(44 px desktop, 48 px phone/tablet, 72 px TV). This is token-data coverage only: it does not enable a renderer or
claim runtime compatibility. Renderer activation remains staged by ADR-0019's platform milestones. Motion
reduction resolves authored durations to zero.

HDR uses the standard numeric reference-white token at 203 cd/m2; the authored highlight ceiling is 203 cd/m2
in SDR and 600 cd/m2 in HDR. The DTCG Color module does not define an HDR colour space. Colours in this base set
are sRGB. Wide-gamut colour authoring, generated sRGB fallbacks and renderer-specific HDR compositing remain
pending #59 and later platform
work; this set makes no claim that those outputs or capabilities are verified.

## Contrast evidence

`tools/design/check_tokens.py` resolves all 64 mode permutations (including both themes, both contrast modes,
all densities, motion settings and dynamic-range settings), resolves aliases after applying ordered Resolver
sources, and calculates every pair declared in semantic token metadata. It also checks the player button's
resting, hover, focus and pressed foreground/background pairs in every permutation at 4.5:1 or higher; the
disabled state is explicitly exempt. The focused button fill uses the accessible control foreground/background
pair, while the separate `player.focus.ring` token supplies the focus indicator. Focus-ring pairs require 3:1.
Authored sRGB components are composited source-over in sRGB before WCAG relative-luminance calculation.
Declared backgrounds are opaque, so those checks do not assume a video backdrop. The translucent scrim is
intentionally not declared as a fixed-background contrast pair; its documented backdrop is the changing video
frame. Generated fallback contrast is pending #59/#60.

Run the focused checks from the repository root:

```sh
python3 tools/design/check_tokens.py
python3 -m unittest discover -s tools/design -p 'test_*.py'
```

These checks cover the DTCG structures used here, alias existence/types/cycles, non-empty descriptions on every
authored token, component metadata, the Resolver inputs and overlays, density coverage, semantic contrast pairs
and enabled button state contrast. They are not the full validator, renderer lint or drift pipeline tracked by
#60.

The taxonomy and pipeline are defined by [ADR-0019](../docs/adr/0019-design-tokens.md), under the UI architecture
in [ADR-0007](../docs/adr/0007-ui-architecture.md). The implementation contract and its reviewed decisions are
in [the ADR-0017/0019 contract](../docs/architecture/adr-0017-0019-implementation-contract.md).
