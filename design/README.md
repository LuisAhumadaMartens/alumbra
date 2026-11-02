# Design tokens

`tokens/` is the source of player design data in the stable DTCG 2025.10
[Format](https://www.designtokens.org/TR/2025.10/format/),
[Color](https://www.designtokens.org/TR/2025.10/color/) and
[Resolver](https://www.designtokens.org/TR/2025.10/resolver/) modules. The sources are portable JSON, and
one command generates the CSS, TypeScript, Swift, Kotlin and C++ outputs described in
[Generated outputs](#generated-outputs). `playground/` is planned for M4.

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
are sRGB. The generator also accepts Display P3 and Rec. 2020 colours and emits gamut-mapped sRGB fallbacks
([colour spaces](#color-spaces)); renderer-specific HDR compositing remains later platform work, and this set
makes no claim that those capabilities are verified.

## Generated outputs

`tools/design/generate_tokens.py` (Python 3.9 or newer, standard library only) is the generator ADR-0019
chose. It resolves the Resolver document in each of the 64 contexts and writes the five output families plus a
reference table, with deterministic ordering and formatting:

| Output | File | Contents |
|--------|------|----------|
| CSS | [`antumbra/generated/design-tokens.css`](../antumbra/generated/design-tokens.css) | Every token as a `--alumbra-…` custom property, with one rule per context |
| TypeScript | [`antumbra/generated/DesignTokens.ts`](../antumbra/generated/DesignTokens.ts) | `Context`, `Tokens`, `Overrides`, `resolveTokens()`, and the names, CSS properties, types and aliases |
| Swift | [`antumbra/generated/DesignTokens.swift`](../antumbra/generated/DesignTokens.swift) | `DesignTokens.Context`, `.Tokens`, `.Overrides` and SwiftUI colours |
| Kotlin | [`antumbra/generated/DesignTokens.kt`](../antumbra/generated/DesignTokens.kt) | Package `org.alumbra.designtokens`: `Context`, `Tokens`, `Overrides` |
| C++ | [`alumbra/design_tokens/design_tokens.h`](../antumbra/design_tokens/include/alumbra/design_tokens/design_tokens.h) | `constexpr` `Resolve()`, `Tokens`, `Overrides` and `VisitTokens()`; CMake target `alumbra::design_tokens` |
| Reference | [`docs/reference/design-tokens.md`](../docs/reference/design-tokens.md) | Every token's tier, type, names, alias, varying modifiers and default |

```sh
python3 tools/design/generate_tokens.py
python3 tools/design/generate_tokens.py --check
```

Generated files are committed; edit the sources and regenerate instead of editing them. `--check` fails when a
committed output differs from what the sources generate. The generator rejects two token paths that map to the
same canonical name, CSS custom property or identifier; a context that adds or removes tokens or changes a
token's type; and a public token whose alias target differs between contexts.

### Tiers in each output

CSS declares every tier, because contexts and aliases resolve through primitive custom properties; renderers
and integrators still use only semantic and component properties. The TypeScript, Swift, Kotlin and C++ outputs
contain only semantic and component tokens, so a renderer cannot reference a primitive. Each public token is
either an alias of another public token or a value token whose value depends on some of the modifiers; the
generated tables hold one value per combination of those modifiers. The reference table lists which ones apply.

### CSS contexts

The base rule declares every default on `:root` and on `:host`. Each other context is a rule on the same element:

```html
<html data-alumbra-theme="light" data-alumbra-density="tv">
<alumbra-player data-alumbra-contrast="high"></alumbra-player>
```

`theme`, `contrast` and `motion` follow `prefers-color-scheme: light`, `prefers-contrast: more` and
`prefers-reduced-motion: reduce` while their attribute is absent; an explicit attribute, including the default
value, wins over the media query. `density` and `dynamic-range` have no media query: the app sets the density,
and the renderer sets `dynamic-range="hdr"` only while HDR video is presented on an HDR-capable output, as
ADR-0019 requires. A custom property resolves `var()` on the element that declares it, so context attributes
belong on the element that receives the stylesheet (`<html>` or the player host), not on a descendant.

### Units

| Token type | CSS | TypeScript, Swift, Kotlin and C++ |
|------------|-----|-----------------------------------|
| `dimension` | As authored, `px` or `rem` | A number of CSS reference pixels; `rem` is multiplied by 16 |
| `duration` | As authored, `ms` or `s` | Milliseconds; `s` is multiplied by 1000. C++ uses `std::chrono::duration<double, std::milli>` |
| `cubicBezier` | `cubic-bezier(x1, y1, x2, y2)` | The four control-point coordinates |
| `fontWeight` | A number from 1 to 1000 | The same number; named DTCG weights are converted (`regular` is 400) |
| `number` | Unitless | Unitless: luminance in cd/m², opacity, line-height ratios and stacking order |

A CSS pixel is device-independent. Renderers map one pixel to one point in SwiftUI and UIKit, to one `dp` in
Compose (`sp` for `typography.font_size.*` and `player.captions.font_size`, so the system font scale applies),
and to one logical pixel on the desktop, which the renderer multiplies by the display's pixel density. SwiftUI
animations take seconds, so Swift renderers divide durations by 1000.

### Colour spaces

sRGB is the baseline: every colour carries its authored `colorSpace`, `components` and `alpha`, plus `srgb`
fallback components. sRGB colours are emitted exactly as authored. A colour authored in `display-p3` or `rec2020`
is converted to sRGB with the [CSS Colour 4 gamut-mapping algorithm](https://www.w3.org/TR/css-color-4/#gamut-mapping)
(chroma reduction in OKLCh until the clipped colour is within a ΔEOK of 0.02), and the result is rounded to five
decimal places so that every output carries the same fallback. Alpha is never gamut mapped.

- **CSS** declares the fallback as `rgb()` and redeclares the authored colour as `color(display-p3 …)` or
  `color(rec2020 …)` inside `@media (color-gamut: p3)` or `(color-gamut: rec2020)` and an `@supports` check for
  that syntax, so a browser without either keeps the sRGB value.
- **Swift** exposes `swiftUIColor`, which uses SwiftUI's Display P3 colour space for Display P3 colours, leaving the
  mapping for sRGB displays to the system's colour management. Rec. 2020 colours use the sRGB fallback there until
  the Apple renderer maps them (#210, M11).
- **TypeScript, Kotlin and C++** expose both forms; each renderer uses the authored form only where the display
  and the platform support it (for example Compose's Display P3 colour space on a wide-colour-gamut window) and
  the fallback elsewhere. The desktop renderer uses the fallback until D-19 (#19) and M11 define wide-gamut and
  HDR compositing.

HDR has no colour space in the DTCG Color module; it is expressed through the luminance number tokens and the
`dynamic-range` context described above.

### Runtime overrides

Each typed output resolves the tokens of one context with optional typed overrides: `Resolve(context, overrides)`
in C++, `resolveTokens(context, overrides)` in TypeScript, `DesignTokens.Tokens(context:overrides:)` in Swift and
`Tokens(context, overrides)` in Kotlin. An override replaces a semantic or component token's value in that
context. Every component that aliases an overridden token is re-resolved from it, and an override on the
component itself wins over the value derived through its alias. An override of a `$deprecated` alias applies to
its replacement unless the replacement is overridden too. In CSS, setting a semantic custom property on the
element that carries the stylesheet re-resolves the components declared there in the same way. The versioned
JSON theme format that the SDKs and the playground will load is not part of this change; its schema, context
selectors and error reporting await review under the
[implementation contract](../docs/architecture/adr-0017-0019-implementation-contract.md#runtime-theme-contract-proposal-for-59).

### Verification

```sh
python3 -m unittest discover -s tools/design -p 'test_*.py'
(cd tools/design && npm ci --ignore-scripts && npm run typecheck && npm run lint:css)
python3 tools/design/golden_tokens.py --css
python3 tools/design/golden_tokens.py --run node tools/design/golden/dump.ts
ctest --preset debug -R design_tokens
```

`tools/design/golden_tokens.py` is the cross-output golden test. It compares every public token in all 64
contexts, and two override scenarios, with values computed from the sources. The TypeScript, Kotlin, Swift and
C++ outputs print their values through the programs in `tools/design/golden/` and
`test/antumbra/design_tokens/`; the CSS is evaluated by simulating the cascade with the context attributes, the
preference media queries and wide-gamut displays. CI pins Node.js, the npm tools in `package-lock.json`
(TypeScript and stylelint), the JDK and the Kotlin compiler. It type-checks TypeScript in strict mode, lints CSS,
compiles Kotlin with `kotlinc -Werror` on Linux and runs `swiftc -typecheck` on macOS. The C++ tests check sample
tokens with `static_assert`, and the installed-consumer test compiles the header at exactly C++20.

## Contrast evidence

`tools/design/check_tokens.py` resolves all 64 mode permutations (including both themes, both contrast modes,
all densities, motion settings and dynamic-range settings), resolves aliases after applying ordered Resolver
sources, and calculates every pair declared in semantic token metadata. It also checks the player button's
resting, hover, focus and pressed foreground/background pairs in every permutation at 4.5:1 or higher; the
disabled state is explicitly exempt. The focused button fill uses the accessible control foreground/background
pair, while the separate `player.focus.ring` token supplies the focus indicator. Focus-ring pairs require 3:1.
Colours are converted to their sRGB fallbacks (unchanged for sRGB colours) and composited source-over in sRGB
before WCAG relative-luminance calculation. Declared backgrounds are opaque, so those checks do not assume a
video backdrop. The translucent scrim is intentionally not declared as a fixed-background contrast pair; its
documented backdrop is the changing video frame. The `tokens_validate` CI job runs these checks on every pull request that touches tokens or the generator.

Run the focused checks from the repository root:

```sh
python3 tools/design/check_tokens.py
python3 -m unittest discover -s tools/design -p 'test_*.py'
```

These checks cover the DTCG types and structures used here, alias existence, types and cycles, the lower_snake naming
rule, non-empty descriptions on every authored token, tier metadata that matches the source file, component
metadata, the Resolver inputs and overlays, density coverage, semantic contrast pairs and enabled button state
contrast. Tier rules are re-checked in every resolved mode:

- a semantic token must alias a primitive token and may not hold a raw value;
- the only semantic-to-semantic alias allowed is one marked `$deprecated`, pointing at its same-tier
  replacement;
- a component token must alias a semantic or component token, never a primitive or a raw value.

A contrast pair whose background is translucent resolves it against its declared opaque `backdrop` token before the
WCAG ratio is computed. Failure messages name the token, the pair or alias target, the measured ratio against the
minimum and the resolver mode.

`tools/design/check_drift.py` copies the token sources and the generated outputs into a scratch repository, commits
them, regenerates the outputs, runs `git diff --exit-code` and prints the diff with the regeneration command
(`python3 tools/design/generate_tokens.py`). It never modifies the working tree, so it catches a hand-edited
generated file, a missing output and a token edit that was not regenerated.

The lint for primitive-token references and raw colour or dimension values in renderer code has nothing to scan
until the renderers exist; the generated outputs already expose only semantic and component tokens.

The taxonomy and pipeline are defined by [ADR-0019](../docs/adr/0019-design-tokens.md), under the UI architecture
in [ADR-0007](../docs/adr/0007-ui-architecture.md). The implementation contract and its reviewed decisions are
in [the ADR-0017/0019 contract](../docs/architecture/adr-0017-0019-implementation-contract.md).
