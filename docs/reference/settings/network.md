# Settings: network

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

16 settings in the `network` namespace. See the [settings reference index](README.md).

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

<a id="network.http.allow_https_to_http_redirect"></a>

## `network.http.allow_https_to_http_redirect`

Whether a transport follows a redirect from an HTTPS URL to an HTTP URL. The default of false refuses it, because it would send the rest of the session in the clear, and the request fails with a redirect error.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.allows_constrained_network_access"></a>

## `network.http.allows_constrained_network_access`

Whether Apple's URLSession may use a constrained network such as Low Data Mode. False makes URLSession fail when only a constrained route is available. Other platforms do not support this policy.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | macOS, iOS, iPadOS, tvOS |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.allows_expensive_network_access"></a>

## `network.http.allows_expensive_network_access`

Whether Apple's URLSession may use an expensive network such as cellular data. Other platforms do not support this policy.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | macOS, iOS, iPadOS, tvOS |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.assume_http3_capable"></a>

## `network.http.assume_http3_capable`

Whether Apple URLSession requests may assume the client supports HTTP/3. The default lets URLSession negotiate normally. Other platforms do not support this policy.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | macOS, iOS, iPadOS, tvOS |
| Mutability | Restart |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.connect_timeout_ms"></a>

## `network.http.connect_timeout_ms`

Time the transport waits for a connection, including the TLS handshake, before it fails the request with a timeout. The transport enforces it because only the stack observes connection setup. Default 10 s follows ADR-0026 and #75.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `10000` |
| Range | `1000` to `60000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.credentials"></a>

## `network.http.credentials`

Fetch credentials mode. A request's explicit include_credentials opt-in selects include regardless of this default. Cross-origin include requires server CORS credentials headers.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"same-origin"` |
| Platforms | Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `omit` | Never send credentials. |
| `same-origin` | Send credentials only to the request's origin. |
| `include` | Send credentials to same-origin and cross-origin requests. |

<a id="network.http.max_redirects"></a>

## `network.http.max_redirects`

Redirects a transport follows before it fails the request. The default and the maximum of 20 follow the Fetch Standard, and this value is the RedirectPolicy limit of every request.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `20` |
| Range | `0` to `20` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.read_timeout_ms"></a>

## `network.http.read_timeout_ms`

Longest time an HTTP byte stream waits without receiving a byte of the response head or body before Umbra cancels the request and fails it with a timeout. Umbra owns this idle-read timer: it does not run while a request is paused, and the first response byte is allowed the connect timeout as well. Default 20 s follows #75.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `20000` |
| Range | `1000` to `120000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.use_priority_hints"></a>

## `network.http.use_priority_hints`

Whether the web transport sends the request priority hint to fetch where the browser supports it.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="network.http.user_agent"></a>

## `network.http.user_agent`

User-Agent that HTTP byte streams send. The empty default sends `Alumbra/&lt;version&gt;`. A web port cannot set this header and ignores the key.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

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
