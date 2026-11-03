# Settings: audio

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `audio` namespace. See the [settings reference index](README.md).

<a id="audio.downmix.mode"></a>

## `audio.downmix.mode`

Audio channel conversion policy. Automatic downmix uses ITU-R BS.775 coefficients; the default is auto.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"auto"` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `auto` | Downmix to the output channel count when needed. |
| `stereo` | Downmix to stereo. |
| `none` | Preserve the input channel count. |

<a id="audio.output.channels"></a>

## `audio.output.channels`

Requested audio output channel count. Zero selects the device default.

| Field | Value |
|-------|-------|
| Type | `integer` |
| Default | `0` |
| Range | `0` to `8` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="audio.resampler.quality"></a>

## `audio.resampler.quality`

Quality preset used when converting audio sample rates. The default is high.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"high"` |
| Platforms | Linux, macOS, Windows |
| Mutability | Next load |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `low` | Use a short resampling filter for lower latency. |
| `medium` | Balance resampling quality and latency. |
| `high` | Use the highest quality resampling filter. |
