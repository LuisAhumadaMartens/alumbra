# Settings: qoe

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `qoe` namespace. See the [settings reference index](README.md).

<a id="qoe.enabled"></a>

## `qoe.enabled`

Collect playback QoE metrics. When false, event computation and snapshot updates are disabled.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Stable |
| Since | 0.1.0 |

<a id="qoe.snapshot_interval_ms"></a>

## `qoe.snapshot_interval_ms`

Recommended interval for publishing refreshed statistics snapshots. Pulling a snapshot always returns the current event-derived model.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `1000` |
| Range | `100` to `60000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Stable |
| Since | 0.1.0 |
