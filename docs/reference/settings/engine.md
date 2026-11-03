# Settings: engine

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

5 settings in the `engine` namespace. See the [settings reference index](README.md).

<a id="engine.threading"></a>

## `engine.threading`

Selects the web engine threading mode. Auto uses pthreads only when cross-origin isolation and SharedArrayBuffer are available; otherwise it uses the single-threaded build.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"auto"` |
| Platforms | Web |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `auto` | Use threads when the page supports isolation. |
| `single` | Use the single-threaded build. |
| `threaded` | Use the threaded build when available. |

<a id="engine.threads.decoder_max"></a>

## `engine.threads.decoder_max`

Shared decoder thread budget. Zero requests a hardware-concurrency-derived port default. Individual decoder libraries must share this budget; M11 validates device policy.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `2147483647` |
| Automatic value | `0` selects `hardware_concurrency_budget` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="engine.threads.max_workers"></a>

## `engine.threads.max_workers`

Worker pool budget. Zero requests a port default derived from hardware concurrency, rather than zero workers. Exact device policy and performance validation belong to #48 and M11.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `2147483647` |
| Automatic value | `0` selects `hardware_concurrency_budget` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="engine.threads.yield_budget_ms"></a>

## `engine.threads.yield_budget_ms`

Single-threaded runner yield budget. Zero requests an automatic budget of half the display frame interval, capped at 8 ms, per ADR-0015. Applies to any single-threaded build; #143 supplies the initial web implementation. The unreleased provisional engine.web.task_budget_ms needs no alias.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `0` |
| Range | `0` to `8` |
| Unit | `ms` |
| Automatic value | `0` selects `half_frame_interval_capped` |
| Platforms | Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="engine.use_worker"></a>

## `engine.use_worker`

Runs the web engine in a dedicated worker when true. Set false for restricted webviews that cannot create workers.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Web |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
