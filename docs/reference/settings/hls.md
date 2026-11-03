# Settings: hls

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

2 settings in the `hls` namespace. See the [settings reference index](README.md).

<a id="hls.keys.allow_insecure_key_uri"></a>

## `hls.keys.allow_insecure_key_uri`

Allows an HTTPS HLS playlist to reference an HTTP key URI when enabled. The default is false.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="hls.parser.strict"></a>

## `hls.parser.strict`

Rejects HLS playlists that violate a SHOULD-level rule when enabled. The default is false.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
