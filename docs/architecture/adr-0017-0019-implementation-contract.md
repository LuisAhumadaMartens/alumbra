# ADR-0017 and ADR-0019 implementation contract

This document makes the accepted settings and design-token decisions discoverable at their implementation
entrypoints and records proposed implementation details raised while preparing #53, #54, #58, #59, #60 and #160.
It does not amend or supersede either accepted ADR. The details marked **Proposed for owner review** are not
settled until the relevant work-item acceptance criteria are reviewed.

## Settings (ADR-0017)

The schema is TOML under `settings/`. Its precedence, from lowest to highest, is compiled default, port default,
integrator configuration, user preference and per-source override. An integrator lock removes the user layer
from resolution for that key, including preferences written before the lock was set.

### Metadata proposal for #53

Keep key introduction, aliases and deprecation as separate metadata:

- `since` records the release in which the key itself was introduced.
- Each alias records its former key name and the release in which that alias was introduced. The alias remains
  compatible for at least one minor release, as required by ADR-0017.
- Deprecation records the release in which deprecation starts and may name a replacement. A deprecated key may
  have no replacement, and deprecation does not imply that the key was renamed.

The exact TOML field names and compatibility-window boundary should be confirmed in #53 before the schema is
implemented. An alias and a deprecation record are not interchangeable.

### `live` apply-mode proposal for #54 and #160

`live` means a valid change needs no source reload or player recreation. It does not promise synchronous
application: the settings store accepts the validated value, then posts a typed notification to the owning
component's sequence. That component observes the change when its sequence processes the notification; queued
work may delay it. Consumers must not rely on an immediate callback or frame boundary.

## Design tokens (ADR-0019)

Tokens remain pure DTCG 2025.10 JSON under `design/tokens/`; the generator and lint check must preserve the
accepted taxonomy and public naming rules. The following proposed contracts address how that source maps into
public outputs and runtime themes.

### Names and generated identifiers proposal for #58 and #59

Use dotted paths whose individual DTCG group/token names are lower_snake components containing only lowercase
ASCII letters, digits and single internal underscores. A component may be numeric (for example `4` or `900`);
it may not contain a dot, hyphen, leading/trailing underscore or consecutive underscores.

The candidate output mappings are:

- **CSS:** `--alumbra-` followed by path components joined with `-`; underscores within a component are
  preserved. Because hyphens are excluded from source components, the mapping is injective. For example,
  `player.ad_band.background` becomes `--alumbra-player-ad_band-background`, distinct from
  `player.ad.band.background` → `--alumbra-player-ad-band-background`.
- **TypeScript, Swift, Kotlin and C++ flat identifiers:** prefix `alumbra_token_`, encode each source underscore
  as `_u`, and join path components with `_s`. Letters and digits are copied. The source grammar excludes literal
  hyphens, and encoding underscores and separators independently makes the mapping injective and valid as an
  identifier in all four languages.

Generation must reject two distinct canonical token paths that map to the same output name, and tests must
exercise both example paths above in every output. A token's default/resting value alongside state children is
represented by the DTCG group `$root` token, not by a sibling `$value` on a group or an implicit missing state.
`$root` is structural and maps to the enclosing group path; it is not appended as a public path segment. These
are proposals for #58/#59 review; no generated token names are being introduced by this documentation.

**Status in #59:** the generator applies these mappings and collision checks in every output, as described in
[Generated outputs](../../design/README.md#generated-outputs).

### Density and rollout proposal for #58

Keep token-data coverage separate from renderer availability. The proposed #58 scope is to define the density
contexts and author usable phone/tablet, desktop and TV values in the M0 token set, while renderer support follows
the platform milestones: desktop in M0/M1, web in M4, Android/Google TV in M6 and Apple in M7. A TV-class web
browser is supported only if selected by D-22's compatibility scope. This preserves #58's form-factor coverage
without moving platform rollout earlier; the exact relationship between authored contexts and activation remains
for owner review in #58.

### Runtime theme contract proposal for #59

The SDKs and playground should exchange one versioned JSON theme shape. Each override is scoped to the Resolver
context(s) in which it applies; an unscoped value must not erase values for other contexts. A theme is validated
against the token's declared type and overridability before application. Unknown token names, invalid contexts
and type-invalid values are rejected without partially applying the document.

When a theme is resolved, alias dependencies are re-evaluated in the active Resolver context. An explicit
component override takes precedence over a value re-derived through an alias. The schema version, exact context
selector shape, and error surface require review in #59 before implementation.

**Status in #59:** each typed output resolves one context with typed overrides, re-resolving aliases and keeping
explicit component overrides, as described in [runtime overrides](../../design/README.md#runtime-overrides). The
versioned JSON theme document, its context selectors and its error surface are not implemented and still await
the review above.

### Validation and lint proposal for #58 and #60

- Semantic tokens reference primitive tokens; a semantic token with a raw value fails validation.
- Renderer code may reference semantic and component tokens, never primitive tokens or raw colour/dimension
  literals.
- The one exception to primitive-only references in the semantic tier is a deprecated semantic alias pointing
  to its same-tier replacement, with `$deprecated` metadata. The alias remains subject to ADR-0019's compatibility
  and runtime-override rules.

The fixtures in #60 should prove both rejection of a raw semantic value and acceptance of that narrow deprecated
alias exception.

## Reference correction

**Erratum to ADR-0019's References list:** its entry labels BT.2408 as a Recommendation and points to the
`R-REC` series. BT.2408 is a **Report**, not a Recommendation. The accepted ADR remains unchanged; the official
publication supporting the 203 cd/m² HDR reference-white value is
[Report ITU-R BT.2408-6 (2023)](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf).
Future references should use that title and source type.
