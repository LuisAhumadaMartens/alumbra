# Settings: playback

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

9 settings in the `playback` namespace. See the [settings reference index](README.md).

<a id="playback.display.rate_match"></a>

## `playback.display.rate_match`

Display refresh-rate matching, ADR-0030. The default is `seamless`.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"seamless"` |
| Platforms | Linux, macOS, Windows |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `off` | Matching is disabled. |
| `seamless` | Only switches that do not blank the screen are made. |
| `always` | Mode switches that blank the screen are also allowed. |

<a id="playback.preserves_pitch"></a>

## `playback.preserves_pitch`

Whether audio pitch stays unchanged when playback rate changes within the audible range. The default is true, following ADR-0024.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.rate.max"></a>

## `playback.rate.max`

Highest playback rate at which audio is audible. The default is 4.0; higher finite rates are accepted with audio muted, following ADR-0024.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `4.0` |
| Range | `0.25` to `4.0` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.rate.min"></a>

## `playback.rate.min`

Lowest playback rate at which audio is audible. The default is 0.25; lower finite non-negative rates are accepted with audio muted, following ADR-0024.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `0.25` |
| Range | `0.25` to `4.0` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.sync.audio_offset_ms"></a>

## `playback.sync.audio_offset_ms`

User audio offset against video. A positive value delays audio, per ADR-0024. The default of 0 applies no offset. Replaces the proposed `audio.output.latency_compensation_ms`.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `-10000` to `10000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.sync.frame_drop_lateness_percent"></a>

## `playback.sync.frame_drop_lateness_percent`

Lateness, as a percentage of the frame interval, beyond which a video frame is dropped. The default of 100 is one whole frame interval, ADR-0024. Replaces the proposed `playback.sync.late_frame_drop_ms`.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `100` |
| Range | `0` to `1000` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.sync.keyframe_skip_lag_ms"></a>

## `playback.sync.keyframe_skip_lag_ms`

Video lag behind the clock that triggers a skip to the next keyframe. The default of 500 ms follows ADR-0024.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `500` |
| Range | `1` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="playback.sync.mode"></a>

## `playback.sync.mode`

Selects the synchronisation master. The default is `audio`, ADR-0024.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"audio"` |
| Platforms | Linux, macOS, Windows |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `audio` | The audio clock drives presentation. |
| `display_resample` | Audio is resampled against the display refresh to stay in step. |

<a id="playback.sync.resample_max_change_percent"></a>

## `playback.sync.resample_max_change_percent`

Largest speed change display resampling may apply. The default of 1.0 follows ADR-0024.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `1.0` |
| Range | `0.0` to `100.0` |
| Unit | `percent` |
| Platforms | Linux, macOS, Windows |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |
