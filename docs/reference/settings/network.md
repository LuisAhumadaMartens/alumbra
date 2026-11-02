# Settings: network

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

6 settings in the `network` namespace. See the [settings reference index](README.md).

<a id="network.cmcd.enabled"></a>

## `network.cmcd.enabled`

Whether Umbra attaches CMCD v1 data to requests. The default is off, following ADR-0020 and ADR-0026. The CMCD task owns the serialisation request filter.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.cmcd.mode"></a>

## `network.cmcd.mode`

Where CMCD data is sent when enabled: header or query. The compiled default is header, which leaves CDN cache keys untouched. The web port default is query through the port-default layer, because CMCD headers are not CORS-safelisted. ADR-0026 lets integrators choose either mode on any platform.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"header"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `header` | Send CMCD data in request headers. |
| `query` | Send CMCD data as a query argument. |

<a id="network.pathway_exclusion_ms"></a>

## `network.pathway_exclusion_ms`

Failed pathway exclusion without Content Steering. The 300 second default follows ADR-0018. With steering, normal exclusion lasts until the next refresh; #127 also preserves longer Retry-After deadlines.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `300000` |
| Range | `0` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.retry.initial_backoff_ms"></a>

## `network.retry.initial_backoff_ms`

Initial jittered exponential retry backoff. Default 500 ms follows ADR-0018/#75. The owning component in #75 validates its relationship to the maximum backoff.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `500` |
| Range | `0` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.retry.max_attempts"></a>

## `network.retry.max_attempts`

Retry attempt budget for transient network failures. ADR-0018/#75 provide the default of 3; #75/#127 own attempt counting, request classification and recovery.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `3` |
| Range | `0` to `2147483647` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.retry.max_backoff_ms"></a>

## `network.retry.max_backoff_ms`

Maximum retry backoff. Default 8000 ms follows ADR-0018/#75. A Retry-After longer than this budget triggers immediate failover, not a longer playback-blocking wait.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `8000` |
| Range | `0` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
