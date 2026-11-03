# Settings: ads

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

19 settings in the `ads` namespace. See the [settings reference index](README.md).

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

<a id="ads.click_through.pause_on_click"></a>

## `ads.click_through.pause_on_click`

Pause ad playback while the host handles a click-through.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
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

<a id="ads.media.max_bitrate_kbps"></a>

## `ads.media.max_bitrate_kbps`

Maximum bitrate for a selected ad media file. Zero applies no cap.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `50000` |
| Unit | `kbps` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.media.preferred_delivery"></a>

## `ads.media.preferred_delivery`

Delivery type accepted when selecting an ad media file.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"any"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Next load |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `progressive` | Prefer progressive ad media. |
| `streaming` | Prefer streaming ad media. |
| `any` | Accept progressive or streaming ad media. |

<a id="ads.prefetch_lead_ms"></a>

## `ads.prefetch_lead_ms`

Time before an ad break to request its source. A zero value requests the source at the break.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `5000` |
| Range | `0` to `60000` |
| Unit | `ms` |
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

<a id="ads.ui.keyboard_shortcuts_during_ads"></a>

## `ads.ui.keyboard_shortcuts_during_ads`

Controls keyboard shortcuts during ads. The default allows play, pause and mute shortcuts.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"media_keys_only"` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `none` | Disable keyboard shortcuts during ads except Escape leaving fullscreen. |
| `media_keys_only` | Allow play, pause and mute shortcuts during ads, plus Escape leaving fullscreen. |
| `all` | Allow all player keyboard shortcuts during ads, while Escape only leaves fullscreen. |

<a id="ads.ui.show_countdown"></a>

## `ads.ui.show_countdown`

Shows the remaining ad time in the ad band.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ads.ui.show_pod_position"></a>

## `ads.ui.show_pod_position`

Shows the current ad position in its pod.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

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
