# 0017 — Define settings in a TOML schema with layered, validated, generated access

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #7

## Context

Every tunable in Alumbra (buffer goals, network timeouts, ABR caps, decoder preferences, caption style, ad
behaviour) lives in one settings registry. One source must generate typed C++ accessors in Umbra, the TS, Kotlin
and Swift surfaces (D-18, #18), the reference documentation (#55) and the playground's settings panel (#160), and
a CI drift check (#56) fails when generated code or docs are stale.

Each setting needs a type, a default, a range or enum values, a unit, platform availability, a stability level,
an apply mode, a deprecation path and documentation text. Values come from several layers: ports adjust defaults
for the device, integrators configure the player, users set preferences, and some sources need per-URL
overrides (D-14, #14). Alumbra is a player, not a platform: the app persists user preferences, and the engine
only receives values.

## Decision drivers

- One reviewable source of truth for every setting and its metadata.
- Generated outputs that cannot drift from the source.
- No new runtime dependency in the engine, and no heavy build-time toolchain.
- Predictable precedence and errors that surface bugs instead of hiding them.
- Keys that are identical on every platform and survive renames.

## Considered options

1. **A — Protocol Buffers as the schema** (custom options, no runtime library).
2. **B — JSON Schema 2020-12** with `x-alumbra-*` keywords.
3. **C — Alumbra's own schema format** with its own generator, as Chromium does for enterprise policies.

## Decision

Chosen option: **C, written in TOML.** All three options need an Alumbra-written generator, so the choice is
about authoring. Setting records are small and regular; a dedicated format keeps them concise and readable in
review, and Python's standard library parses TOML, so the generator needs no extra packages. Protocol Buffers
would add `protoc` and a protobuf runtime for the generator and spread docs text across option syntax; JSON
Schema leaves our extension keywords unchecked and is the weakest starting point for C++, Kotlin and Swift.
The protobuf runtime is never part of the engine.

### Schema

- The schema lives under `settings/`, one TOML file per area, each setting a record with: key, type, default,
  range or allowed values, unit, platforms, audience (integrator or end user), stability (`stable` or
  `experimental`), apply mode, deprecation (alias of a former key and the release that introduced it) and
  documentation text.
- Types are boolean, integer, double, enum, string, string list, duration and byte size. Durations and byte sizes
  are integers whose unit the key's suffix states; string lists cover keys such as `observability.log.modules`.
- A stdlib-only Python generator (Python 3.11 or newer, for `tomllib`) validates the schema and emits the C++
  registry and typed accessors, the SDK surfaces, the Markdown reference and a JSON Schema export for the
  playground. Generated files are committed and checked for drift in CI, so ordinary builds need neither Python
  3.11 nor the generator.

### Naming

- Keys are dotted `lower_snake` names grouped by area, such as `buffer.`, `network.`, `abr.`, `text.`, `ads.`,
  `ui.` and `engine.`; for example `buffer.forward_goal_ms`.
- A unit is always a suffix (`_ms`, `_bytes`, `_bps`, `_percent`), and the generator checks that it matches the
  declared unit.
- Booleans are positive (`cmcd.enabled`, never `disable_cmcd`).
- Platforms never appear in keys; availability is metadata, so a key is the same everywhere.

### Precedence

Values resolve from lowest to highest priority:

1. compiled default;
2. port default, for device classes such as 1 GB-class TVs;
3. integrator configuration;
4. user preference, supplied by the app;
5. per-source override.

An integrator may **lock** a key, as Chromium distinguishes managed from recommended policies. A locked key
ignores the user layer entirely, including user values stored before the lock was set, and writing it from the
user layer is rejected like any other invalid value.

### Validation

Values are checked against the declared type and range when they are set. An invalid value is rejected with a
structured error (D-09, #9), and the previous valid value stays in effect. Values are never clamped silently.

### Apply modes

Each key declares when a change takes effect:

- `live`: immediately; the owning component receives a typed change notification posted to its sequence
  ([ADR-0015](0015-threading-and-task-runners.md)), so it never observes a half-applied change;
- `next_load`: at the next source load;
- `restart`: when a new player is created.

### Platform availability

Every key is exposed by every SDK. On a platform where a key has no effect, setting it is accepted and ignored,
with a warning in debug builds, and `isSupported(key)` reports availability. Shared configuration code therefore
works on every platform.

### Renames and removals

A renamed key keeps its former name as an alias for at least one minor release, and using the alias logs a
deprecation warning. After 1.0, keys are removed only in a major release; the versioning itself is D-34's (#34).
Before 1.0, renamed keys still keep their alias for one minor release.

### Per-source overrides

The API takes per-source overrides as a structured object at load time, for example
`load(url, {overrides: {"buffer.forward_goal_ms": 4000}})`, validated like any other value. Only `live` and
`next_load` keys may be overridden per source: the player already exists when a source loads, so a `restart` key
in an override is rejected with a structured error. Whether the CLI also accepts VLC-style options inside URLs is
D-14's decision.

### Keys recorded earlier

The provisional keys of ADR-0015 already follow these rules and move into the schema unchanged unless #53
renames them through the alias policy.

## Consequences

**Positive**
- One readable source generates code, SDK surfaces, documentation and the playground panel, and CI keeps them in
  step.
- The engine gains no runtime dependency, and ordinary builds need no generator toolchain.
- Locks, precedence and validation behave the same in every SDK.
- Shared configuration code works on every platform.

**Negative / costs**
- Alumbra owns a schema format, its validator and its generator.
- Regenerating requires Python 3.11 or newer; macOS's bundled Python is older, so maintainers install a newer one.
- Rejecting invalid values means an integrator that passes an out-of-range value gets an error rather than a
  nearby value.

**Follow-ups**
- #53 defines the TOML schema format and the first keys.
- #54 generates the C++ registry and runtime, including layers, locks, validation and notifications.
- #55 generates the Markdown reference; #56 adds the drift check; #160 consumes the JSON Schema export.
- D-09 (#9) defines the error codes for rejected settings, D-14 (#14) the URL option syntax, D-18 (#18) the SDK
  surfaces and D-34 (#34) the versioning that the rename policy relies on.

## References

- [D-07 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/7)
- [Chromium: policy_definitions (YAML)](https://chromium.googlesource.com/chromium/src/+/main/components/policy/resources/templates/policy_definitions/)
- [Chromium: components/prefs/pref_value_store.h](https://chromium.googlesource.com/chromium/src/+/main/components/prefs/pref_value_store.h)
- [GStreamer: gstparamspecs.h (mutable flags)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/subprojects/gstreamer/gst/gstparamspecs.h)
- [Shaka Player: Configuration tutorial](https://shaka-player-demo.appspot.com/docs/api/tutorial-config.html)
- [TOML v1.0.0](https://toml.io/en/v1.0.0)
- [Python: tomllib](https://docs.python.org/3/library/tomllib.html)
- [Protobuf: Custom options](https://protobuf.dev/programming-guides/proto3/#customoptions)
- [JSON Schema 2020-12](https://json-schema.org/draft/2020-12)
