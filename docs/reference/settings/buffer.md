# Settings: buffer

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

6 settings in the `buffer` namespace. See the [settings reference index](README.md).

<a id="buffer.audio_max_bytes"></a>

## `buffer.audio_max_bytes`

Byte budget for buffered audio samples. The 16 MiB default is provisional until #124 settles it. A port that reports a budget lowers it through the port-default layer, and Umbra never writes beyond a port grant.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `16777216` |
| Range | `1048576` to `8589934592` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="buffer.back_goal_ms"></a>

## `buffer.back_goal_ms`

Amount of media retained behind the playhead, measured from the last keyframe. The 10 s default follows ADR-0034 and is a starting value that #122 and #124 tune. Eviction behind the playhead precedes any reduction of the forward goal. DVR seeks use the manifest window, not this value.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `10000` |
| Range | `0` to `600000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="buffer.forward_goal_ms"></a>

## `buffer.forward_goal_ms`

Target amount of media buffered ahead of the playhead. The 30 s default follows ADR-0034 and is a starting value that #122 and #124 tune. The goal only bounds how far the loader and sample queue run ahead, is clamped by the byte budgets and is raised to one segment if it would fall below that. Live playback caps it at the live-edge distance.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `30000` |
| Range | `1000` to `600000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="buffer.rebuffer_threshold_ms"></a>

## `buffer.rebuffer_threshold_ms`

Buffered media required before playback resumes after a stall. It is larger than the start threshold. The default of about 4 s follows ADR-0034 and is a starting value that #124 tunes.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `4000` |
| Range | `0` to `60000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="buffer.start_threshold_ms"></a>

## `buffer.start_threshold_ms`

Buffered media required before playback starts or resumes after a seek. The default of about 1 s follows ADR-0034 and is a starting value that #124 tunes.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `1000` |
| Range | `0` to `60000` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="buffer.video_max_bytes"></a>

## `buffer.video_max_bytes`

Byte budget for buffered video samples, including retained decoder samples. The 128 MiB default is provisional until #124 settles it. A port that reports a budget, such as the web MSE quota or a TV, lowers it through the port-default layer, and Umbra never writes beyond a port grant. Phones and TV browsers need lower values.

| Field | Value |
|-------|-------|
| Type | `byte_size` |
| Default | `134217728` |
| Range | `1048576` to `8589934592` |
| Unit | `bytes` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
