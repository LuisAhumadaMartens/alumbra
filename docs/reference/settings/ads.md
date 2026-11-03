# Settings: ads

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `ads` namespace. See the [settings reference index](README.md).

<a id="ads.vast.max_response_bytes"></a>

## `ads.vast.max_response_bytes`

Maximum body size accepted for each VAST response.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `1048576` |
| Range | `65536` to `8388608` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.vast.max_wrapper_depth"></a>

## `ads.vast.max_wrapper_depth`

Maximum number of VAST wrappers followed for one ad response. The default accepts the five wrapper levels required by VAST.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `5` |
| Range | `1` to `10` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.vast.request_timeout_ms"></a>

## `ads.vast.request_timeout_ms`

Time allowed for each VAST wrapper request.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `5000` |
| Range | `1000` to `30000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
