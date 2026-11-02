# Settings: player

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

1 setting in the `player` namespace. See the [settings reference index](README.md).

<a id="player.events.timeupdate_interval_ms"></a>

## `player.events.timeupdate_interval_ms`

Interval between timeupdate events during playback, within the 15 to 250 ms that HTML allows. The 250 ms default follows ADR-0015; this does not cap video presentation or audio callbacks.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `250` |
| Range | `15` to `250` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
