# Settings: ads

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

9 settings in the `ads` namespace. See the [settings reference index](README.md).

<a id="ads.beacon.timeout_ms"></a>

## `ads.beacon.timeout_ms`

Maximum time allowed for one tracking beacon request, in milliseconds. The default is 5000 and the supported range is 500 to 30000.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `5000` |
| Range | `500` to `30000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.consent.child_directed.enabled"></a>

## `ads.consent.child_directed.enabled`

Signals the integrator's child-directed treatment choice. The default is `false`; Alumbra passes the flag through without interpreting or storing it.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.consent.gpp_string"></a>

## `ads.consent.gpp_string`

The integrator-supplied IAB GPP consent string. Alumbra passes it unchanged where the provider does not read it itself and never interprets, stores or logs it.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.consent.limited_ads.enabled"></a>

## `ads.consent.limited_ads.enabled`

Signals the integrator's limited-ads choice. The default is `false`; Alumbra passes the flag through without interpreting or storing it.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `false` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.consent.tcf_string"></a>

## `ads.consent.tcf_string`

The integrator-supplied IAB TCF consent string. Alumbra passes it unchanged where the provider does not read it itself and never interprets, stores or logs it.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.enabled"></a>

## `ads.enabled`

Enables client-side ad requests and playback. The default is `true`; disabled ads do not invoke a provider.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.preload.enabled"></a>

## `ads.preload.enabled`

Allows the next scheduled ad to be requested ahead of its break. The default is `true`, subject to the ad memory budget.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.request_timeout_ms"></a>

## `ads.request_timeout_ms`

Maximum time for an ad request, in milliseconds. The default is 5000 and the supported range is 1000 to 30000.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `5000` |
| Range | `1000` to `30000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.seek.play_skipped_breaks"></a>

## `ads.seek.play_skipped_breaks`

Controls snapback after a forward seek crosses ad breaks. The default is `last`, shared by client-side and stitched ad timelines.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"last"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `last` | Play the last ad break crossed by a forward seek. |
| `all` | Play every ad break crossed by a forward seek. |
| `none` | Skip every ad break crossed by a forward seek. |
