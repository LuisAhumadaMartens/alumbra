# Settings: streaming

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `streaming` namespace. See the [settings reference index](README.md).

<a id="streaming.content_steering.enabled"></a>

## `streaming.content_steering.enabled`

Whether Umbra polls the steering manifest of HLS Content Steering and DASH Content Steering and orders pathways by its priority list. When false, pathways keep the order of the manifest. #127 owns the polling and its time to live handling.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="streaming.failover.enabled"></a>

## `streaming.failover.enabled`

Whether the recovery ladder may fail over to redundant variants, alternative BaseURL elements and other pathways, and exclude a failed rendition so that adaptive selection picks another. When false, a request is only retried with backoff and then fails playback. ADR-0018 defines the ladder and #127 implements it.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
