# Settings: abr

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

1 setting in the `abr` namespace. See the [settings reference index](README.md).

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
