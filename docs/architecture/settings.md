# Settings code generation and C++ runtime

[ADR-0017](../adr/0017-settings-schema-and-configuration.md) defines the schema, precedence, validation and apply
modes. This page describes the generator that turns the schema into typed C++ and the `umbra::settings` runtime
([#54](https://github.com/LuisAhumadaMartens/alumbra/issues/54)). The runtime lives in `umbra/settings/` and is a
non-installed target, `umbra::settings`: its headers use Abseil `Status` and Alumbra task runners, so they follow the
same internal-header rule as `umbra/base/internal_include`. A public settings surface belongs to the SDK work.

## Generator

```sh
python3 tools/settings/generate.py            # regenerate every committed output
python3 tools/settings/generate.py --check    # fail if a committed output is stale
```

Python 3.11 or newer, standard library only. Ordinary C++ builds never run it: the outputs are committed so reviewers
see the effect of a schema change. Output is deterministic (settings are sorted by key; nothing depends on time, paths
or hash order) and is formatted as `scripts/format` expects, so `scripts/format --check`, `scripts/tidy` and the C++
policy check cover generated files like hand-written ones. Generated files contain no comments.

| Profile | Schema | Outputs |
|---------|--------|---------|
| `umbra` | `settings/schema/` | `umbra/settings/internal_include/umbra/settings/settings_keys.h`, `umbra/settings/settings_registry.cc`, `umbra/settings/internal_include/umbra/settings/platform.h` |
| `test_all_types` | `tools/settings/fixtures/valid/` | `test/umbra/settings/all_types/`: a registry that exercises all eight types, an alias and a deprecation |

Never edit an output. Fix `tools/settings/emitters/cpp.py` and regenerate.

### Pluggable emitters

`tools/settings/validate.py` validates the schema, `tools/settings/model.py` turns it into a language-neutral `Model`
(sorted settings, platform indices, aliases), and an emitter renders a `Model` into `{path: content}`. An emitter is a
subclass of `emitters.Emitter` registered in `emitters/__init__.py`; `generate.py` profiles name the schema directory,
the emitter and its options. The TypeScript, Kotlin and Swift outputs of ADR-0017 add an emitter and a profile and
never touch validation or the C++ emitter. `tools/settings/test_generate.py` proves the contract with a fake emitter.

The C++ emitter rejects two keys that map to the same constant (`a.b_c` and `a_b.c` would both be `kABC`) and enum
values that map to the same enumerator or are not identifiers.

## Generated API

```cpp
inline constexpr Key<std::int64_t> kNetworkRetryMaxAttempts{6};
inline constexpr Key<absl::Duration> kPlayerEventsTimeupdateIntervalMs{11};
inline constexpr Key<ObservabilityLogLevel> kObservabilityLogLevel{9};

enum class ObservabilityLogLevel { kVerbose, kDebug, kInfo, kWarning, kError };
```

| Schema type | C++ value type |
|-------------|----------------|
| `boolean` | `bool` |
| `integer` | `std::int64_t` |
| `double` | `double` |
| `enum` | generated `enum class` named after the key |
| `string` | `std::string` |
| `string_list` | `std::vector<std::string>` |
| `duration` | `absl::Duration`, whole milliseconds only |
| `byte_size` | `std::uint64_t` |

`Key<T>` is an index into the generated registry. Every typed entry point takes a `Key<T>`, so
`store.Get<bool>(kNetworkRetryMaxAttempts)` does not compile: `settings_typed_get_rejects_wrong_type` builds a
matching-type control and the wrong-type call and requires the second to fail. The registry (`GetRegistry()`) holds,
per key, the type, apply mode, audience, stability, platforms, deprecation, automatic-value policy, range or allowed
values and default. Settings and aliases are sorted by name, so lookup is a binary search. The generated validators
are data-driven: `FindViolation` checks the type, range and allowed values from that metadata.

## Store

```cpp
Store store(GetRegistry(), Platform::kLinux);
absl::Status status = store.Set(Layer::kUser, kNetworkRetryMaxAttempts, 5);
std::int64_t attempts = store.Get(kNetworkRetryMaxAttempts);
store.Reset(Layer::kUser, kNetworkRetryMaxAttempts);
```

- **Layers**, lowest to highest: compiled default, `kPortDefault`, `kIntegrator`, `kUser`, `kSource`. `Get` returns the
  highest layer that holds a value. `Reset(layer)` clears a whole layer and `Reset(layer, key)` one key; the lower value
  shows through again.
- **Validation.** A value that fails its type, range or allowed values, or a duration that is not a whole number of
  milliseconds, returns `InvalidArgument` (catalogue code `SettingsInvalid`, 8001) and leaves the store unchanged.
  Values are never clamped. The error detail context names `setting`, `setting_value`, `reason` and `allowed`.
- **Locks.** `SetUserLocked(key, true)` removes the user layer from resolution for that key, including older values,
  and rejects user writes.
- **Per-source overrides.** `Layer::kSource` accepts `live` and `next_load` keys only; a `restart` key is rejected.
- **Platforms.** A write to a key that the store's platform does not support is validated, accepted and ignored.
  `IsSupported(key)` reports availability. The debug-build warning needs the logging facade (#50).
- **Snapshots.** `TakeSnapshot()` returns an immutable copy of every effective value. A player takes one at creation
  and reads `restart` keys from it (and `next_load` keys when a source loads), so later writes do not change a running
  player.
- **Aliases.** `SetFromString` resolves a former key name to its current key. The deprecation warning needs #50.
- **Concurrency.** One lock guards the layers, so reads during updates are race-free (covered by
  `StoreConcurrencyTest` under the `tsan` preset). `Get` copies the value out.

### Change notifications

```cpp
auto subscription = store.Observe<ObservabilityLogLevel>(
    kObservabilityLogLevel, runner,
    [](const ObservabilityLogLevel& level) { /* on `runner` */ });
```

Only `live` keys can be observed; others return `FailedPrecondition` (read them through a snapshot). A notification is
posted to the subscriber's `SequencedTaskRunner` whenever the **effective** value changes and never otherwise: a write
that is shadowed by a higher layer, repeats the current value or is rejected posts nothing, and one `Reset(layer)`
posts one notification per key that actually changed. Delivery is asynchronous (ADR-0015, and the `live` proposal in the
[implementation contract](adr-0017-0019-implementation-contract.md#live-apply-mode-proposal-for-54-and-160)); a callback
never runs inside the call that changed the value, and tests drive it with `TestTaskEnvironment::RunUntilIdle()`.
Notifications are posted while the store lock is held so that a subscriber sees values in the order they took effect;
`PostTask` does not run tasks inline. `Subscription::Cancel()` (or destroying it) stops delivery: a callback already
running finishes, and one that has not started is dropped. Cancel on the subscriber's own runner for a strict guarantee.
Pending notifications hold only the subscription state, so they outlive the store safely.

## `key=value` parsing

`ParseAssignment` splits at the first `=`; `ParseValue(info, text, redactor)` parses by type; `Store::SetFromString` and
`SetFromAssignment` combine them (this is what `--set` in #99 calls).

| Type | Text |
|------|------|
| `boolean` | `true` or `false` |
| `integer`, `duration`, `byte_size` | optional `-`, decimal digits (milliseconds and bytes respectively); no `+`, spaces or suffixes |
| `double` | a decimal or exponent number without surrounding spaces; `nan` and `inf` are rejected as out of range |
| `enum`, `string` | the text as is |
| `string_list` | comma-separated, no empty element; empty text is the empty list |

Every error names the key, the value and what is allowed, for example
`ALUMBRA-8001: Setting value is invalid: test.count=9 outside the allowed range (allowed: an integer from -5 to 5)`.
`settings_value_parser_fuzzer` feeds arbitrary bytes through both parsers and checks that the result is `InvalidArgument`
or a value that satisfies its metadata and parses back from its formatted text to the same value.

## Tests

`umbra_settings_unittests` (store, layers, locks, observers, parser, registry, all eight types and the concurrency
test), `settings_typed_get_rejects_wrong_type`, `settings_codegen_drift` and `settings_codegen_generator`
(CTest, when Python is 3.11 or newer), and `fuzz_replay.settings_value_parser_fuzzer`. The CI drift job is #56.
