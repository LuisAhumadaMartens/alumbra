# Settings — every option in one schema

[ADR-0017](../docs/adr/0017-settings-schema-and-configuration.md) is the accepted contract. This directory
implements its TOML format and validation. The C++ generator and `umbra::settings` runtime
([#54](https://github.com/LuisAhumadaMartens/alumbra/issues/54)) are described in
[Settings code generation and C++ runtime](../docs/architecture/settings.md); the reference generator (#55), the
generated-output drift check in CI (#56) and SDK generation or the playground (#160) are separate. Ordinary C++ builds
need neither the validator nor the generator.

## Layout and ownership

`schema/<area>.toml` contains one namespace's records. The first dotted component must match the file stem;
the owning area reviews its keys. `observability` owns logging/tracing, `network` owns transport/retry,
`abr` owns rendition selection, `engine` owns runner budgets, `player` owns public event delivery and
`source` owns data-source buffering and type detection.
`meta-schema.toml` is the machine-readable format vocabulary used by the stdlib-only Python validator.
This is an Alumbra TOML meta-schema, not JSON Schema; the JSON Schema export for the playground belongs to
the generator. Test-only keys live under `tools/settings/fixtures/`, never the production schema.

Each area file has `schema_version = 1`, `release = "major.minor.patch"` and a nonempty `[[settings]]` array.
All files share the same release. `schema_version` versions the file format; `release` identifies the target
settings release. The initial `0.1.0` is a provisional settings coordinate, not a released SDK/versioning
decision (D-34). Releases are three unsigned decimal components, without leading zeros or prerelease/build
suffixes. Unknown fields are errors, including unknown nested metadata.

## Record fields

| Field | Contract |
|-------|----------|
| `key` | Unique dotted ASCII lower_snake name with at least two components. Each component starts with a lowercase letter; single internal underscores and digits are allowed. No platform components or negative `disable`/`disabled` names. |
| `type` | `boolean`, `integer`, `double`, `enum`, `string`, `string_list`, `duration`, `byte_size`. |
| `default` | A value of exactly the declared type. Booleans are not integers. Doubles use TOML floats (`1.0`, not `1`) and must be finite. |
| `range` | Required `{ min, max }` inclusive bounds for numeric types; forbidden otherwise. Integer values/bounds fit signed 64 bits. Duration and byte-size bounds are nonnegative. |
| `values` | Nonempty array of `{ value, description }` entries. Required for enums; optional for strings/string lists. Values are unique, nonempty strings, every value has nonempty Markdown documentation, and defaults must belong to this set. A string-list constraint applies to each element. |
| `unit` | `""` (unitless), `ms`, `bytes`, `bps`, `percent`. Must match the leaf suffix exactly. Durations require `_ms`; byte sizes require `_bytes`; other units apply only to integers/doubles. |
| `platforms` | Nonempty, unique list drawn from `linux`, `macos`, `windows`, `android`, `google_tv`, `ios`, `ipados`, `tvos`, `web`. Availability is metadata, not part of the public name. |
| `audience` | `integrator` or `end_user`. |
| `stability` | `stable` or `experimental`. Experimental does not authorise invented behaviour or unresolved defaults. |
| `apply` | `live`, `next_load`, `restart`, following ADR-0017 instead of the issue's startup/runtime split. |
| `since` | Key introduction release, no later than the schema's release. |
| `description` | Nonempty Markdown documenting semantics, default, special values and any constraints the owning component must implement. |
| `auto` | Optional explicit reserved-value policy; see below. Without this field, zero is a literal value, not automatic. |
| `aliases` | Optional array of separate former-name records; see compatibility below. |
| `deprecation` | Optional separate deprecation record; a replacement is not required. |

Use positive feature switches such as `cmcd.enabled`, never `disable_cmcd`. Put units in leaf suffixes,
not intermediate components: `buffer.forward_goal_ms`, not `buffer.ms.forward_goal`. Names such as
`engine.web.task_budget_ms` are rejected. Its unreleased replacement is `engine.threads.yield_budget_ms`,
so no compatibility alias is needed. Aliases also obey naming and unit-suffix rules.

## First production keys and pending defaults

`observability.log.level` is `live`, defaults to `info`, and documents `verbose`, `debug`, `info`, `warning`,
`error`. It can only restrict severities retained by the build-time floor; enabling compiled-out severities
requires rebuilding. Both category lists are `live` and default to empty. An empty trace list disables tracing.
The logging default is an empty category filter; #50 owns filter application. Both lists constrain entries to
ADR-0020's shared categories: `net`, `manifest`, `abr`, `buffer`, `demux`, `decode`, `render`, `audio`, `text`,
`ads`, `drm`, `settings`, `player`, `port`.

The accepted ADRs also explicitly route provisional keys through #53:

| Keys | Default and readiness |
|------|-----------------------|
| `engine.threads.max_workers`, `engine.threads.decoder_max` | Explicit automatic hardware-derived budgets, not fixed counts; `restart`. |
| `engine.threads.yield_budget_ms` | Explicit automatic half-frame-interval budget capped at 8 ms; `live`. Initial availability is web (#143), but its name applies to any single-threaded build. |
| `player.events.timeupdate_interval_ms` | 250 ms, from ADR-0015; `live`. |
| `network.retry.max_attempts` | 3, from ADR-0018/#75; `next_load`. #75 owns retry interpretation. |
| `network.retry.initial_backoff_ms`, `network.retry.max_backoff_ms` | 500 / 8000 ms, from ADR-0018/#75; `next_load`. #75 owns cross-key ordering checks. |
| `source.read_ahead_bytes`, `source.type_hint` | 2 MiB (65536 to 67108864) from #73 and `auto` from ADR-0027; `next_load`. #73 owns the buffered reader and type detection that read them. |
| `abr.rendition_exclusion_ms`, `network.pathway_exclusion_ms` | 60000 / 300000 ms, from ADR-0018; `next_load`. #127 owns steering/Retry-After interaction. |
| `playback.stall.timeout_ms`, `playback.gap_jump.max_gap_ms` | **Pending**: ADR-0018 names them without defaults/ranges. Reserved here, not emitted as production records until the recovery/watchdog owner (#127) settles their behaviour. |

The nine implemented provisional keys are `experimental`. Numeric ranges not fixed by an ADR are broad
representation/safety bounds rather than performance recommendations; the consuming follow-up must validate
device policy and cross-key relationships. No watchdog defaults are invented.

### Automatic values are distinct from literal zero

An automatic record explicitly declares `auto = { value = 0, policy = "hardware_concurrency_budget" }` on
unitless integers, or `auto = { value = 0, policy = "half_frame_interval_capped" }` on millisecond durations
with range 0–8. The validator requires the reserved integer value and default to be zero and rejects unknown
or type-incompatible policies. Positive values are explicit budgets. Zero on these records means automatic,
never a zero-worker pool or zero time budget.

Worker/decoder budgets **derive from** hardware concurrency; they need not equal it, and M11 validates the
numeric device policy. The yield formula is already decided by ADR-0015, including sub-millisecond precision
when resolved internally. #54/#143 and the owning port resolve these policies; this schema-only change does
not implement resolution. The schema's integer default is an encoding, not a silently computed fallback.

## Compatibility metadata

Key introduction, aliases and deprecation are independent, implementing
[the accepted metadata proposal](../docs/architecture/adr-0017-0019-implementation-contract.md#metadata-proposal-for-53).

```toml
since = "0.1.0"
aliases = [{ key = "buffer.old_goal_ms", since = "0.1.0", remove_in = "0.3.0" }]
deprecation = { since = "0.1.0", replacement = "buffer.new_goal_ms" }
```

Each alias requires `key` and `since` (the alias-introduction release), with optional `remove_in`.
Aliases are globally unique and may not collide with canonical names. Their introduction is between the
target key's introduction and the current schema release. Alias target and introduction are immutable.
When compared with a previous snapshot, an alias absent from that snapshot must declare the current target
schema release as its introduction, including canonical-to-alias renames. This also permits additions across
sequential PRs targeting the same unreleased schema release, without allowing backdating.

The conservative minimum window retains an alias **through the entire next minor release**. For an alias
introduced at `(major, minor, patch)`, the earliest removal coordinate is `(major, minor + 2, 0)`, compared
lexicographically. Thus `0.1.9` is supported throughout `0.2.x` and may first be removed at `0.3.0`.
Removal has patch zero; a removal coordinate at or after 1.0 must additionally start a new major
(`major.0.0`) and satisfy the same minimum window, even for aliases introduced before 1.0 or upgrades crossing
1.0 directly. Explicit `remove_in` promises cannot be removed or shortened in subsequent schemas, but may
be extended. Omitting it initially means no scheduled removal, not permission for early deletion. An alias
still present at/after its explicit `remove_in` is an error; extend the promise or remove it legally.

`deprecation = { since = "0.1.0" }` is valid with **no replacement**. A replacement, if given, is another
canonical, nondeprecated key of the same type; aliases, self-references and chains/cycles are rejected.
Deprecation cannot precede key introduction or start after the schema release. It does not rename the key.
Alias use warns in the future runtime; this validator has no runtime logging.

History validation rejects early alias deletion, resetting introduction, retargeting, shortened promises,
alias-name reuse as a canonical key, changed canonical introduction/type or deprecation start, and post-1.0
minor-release removals.
CI compares with the PR base (or previous push). That proves compatibility only with that revision, **not**
every previously released schema: release tooling must retain published snapshots and compare each applicable
one using `--previous`. Historical aliases not present in the chosen snapshot cannot be inferred.

## Runtime contract retained for #54

Precedence is compiled default → port default → integrator configuration → user preference → per-source
override. An integrator lock excludes all user-layer values, including older persisted values, and rejects user
writes. Invalid values are rejected with structured errors and retain the previous valid value; never clamp.

`live` changes post typed notifications to the owning sequence; they are not synchronous. `next_load` applies
at the next source load; `restart` requires a new player. Per-source overrides allow only `live`/`next_load`.
Every SDK exposes every key. Unsupported-platform writes are accepted and ignored, warn in debug builds, and
`isSupported(key)` reports availability. Apps persist user preferences; the engine does not.

## Validation

Python **3.11+**, standard library only:

```sh
python3 tools/settings/validate.py
python3 -m unittest discover -s tools/settings -p 'test_*.py'
python3 tools/settings/fuzz_validate.py --smoke 2000
python3 tools/settings/validate.py --previous-ref origin/main
python3 tools/settings/validate.py --previous /path/to/published/settings/schema
python3 tools/settings/generate.py
python3 tools/settings/generate.py --check
```

The validator fails with a path and rule on malformed TOML or metadata. The test suite includes passing keys
covering all eight types, the five issue-mandated failing fixtures, type/range/unit/enum/category failures,
auto-policy checks and alias/deprecation/history cases. The byte-input fuzz target reads stdin by default;
CI runs deterministic mutations of the valid TOML seed as a reproducible smoke check, not exhaustive fuzzing.
The mandatory **Validate settings** CI job runs validation, history checks, tests and fuzz smoke on every PR,
push to main and manual run, and is included in `ci-ok` even if C++ builds are skipped.
