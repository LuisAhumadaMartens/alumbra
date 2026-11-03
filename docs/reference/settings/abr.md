# Settings: abr

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

8 settings in the `abr` namespace. See the [settings reference index](README.md).

<a id="abr.bandwidth.min_sample_bytes"></a>

## `abr.bandwidth.min_sample_bytes`

Minimum body size for a bandwidth sample, filtering manifests, keys and likely cache hits. The default is 16 KiB; #119 consumes this value.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `16384` |
| Range | `1024` to `1048576` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.default_bandwidth_bps"></a>

## `abr.default_bandwidth_bps`

Bandwidth estimate in bit/s used before any sample exists, unless the app supplies a persisted estimate. The 1000000 bit/s default is provisional, in line with common player defaults, until #119 and #122 tune it. Startup picks the highest rendition that fits a conservative fraction of it.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `1000000` |
| Range | `100000` to `1000000000` |
| Unit | `bps` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.estimator.fast_half_life_ms"></a>

## `abr.estimator.fast_half_life_ms`

Half-life of the fast exponentially weighted bandwidth estimate, which reacts quickly to drops. ADR-0033 fixes the key and unit. The 2000 ms default is provisional, taken from the Shaka Player estimator, until #119 and #122 tune it. #119 owns the estimator and its relationship to the slow half-life.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `2000` |
| Range | `1` to `600000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.estimator.slow_half_life_ms"></a>

## `abr.estimator.slow_half_life_ms`

Half-life of the slow exponentially weighted bandwidth estimate, which smooths steady-state noise. ADR-0033 fixes the key and unit. The 5000 ms default is provisional, taken from the Shaka Player estimator, until #119 and #122 tune it. #119 validates that it is not shorter than the fast half-life.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `5000` |
| Range | `1` to `600000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.max_bitrate_bps"></a>

## `abr.max_bitrate_bps`

Maximum rendition bitrate in bit/s that selection may choose. The value 0 means no cap. ADR-0033 applies this cap before any selection rule votes. #120 owns the rule engine.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `9223372036854775807` |
| Unit | `bps` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.max_height"></a>

## `abr.max_height`

Maximum rendition height in pixels that selection may choose. The value 0 means no cap. ADR-0033 applies this cap before any selection rule votes. #120 owns the rule engine.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `2147483647` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.rendition_exclusion_ms"></a>

## `abr.rendition_exclusion_ms`

Failed rendition exclusion duration. The 60 second default follows ADR-0018; #127 owns recovery ladder application.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `60000` |
| Range | `0` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="abr.switch_buffer_ms"></a>

## `abr.switch_buffer_ms`

Buffered media required before the selection logic may switch up to a higher rendition. ADR-0033 renames the issue's seconds key to milliseconds. The 8000 ms default is provisional until #120 and #122 tune it. Switching down is never gated by this value.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `8000` |
| Range | `0` to `60000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
