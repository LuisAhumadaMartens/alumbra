# Settings: audio

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

1 setting in the `audio` namespace. See the [settings reference index](README.md).

<a id="audio.output.device"></a>

## `audio.output.device`

Name of the audio output device that the desktop audio sink opens, as SDL reports it. The empty default opens the system default device, which SDL follows when the user changes it. A name that no device has makes the sink fail to start with a not-found error. The sink never falls back to the default device at start, only after a named device is removed during playback. `audio.output.buffer_frames` is not a key: the sink keeps SDL's device default until the sync measurements of #93 justify another value.

| Field | Value |
|-------|-------|
| Type | `string` |
| Default | `""` |
| Platforms | Linux, macOS |
| Mutability | Restart |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |
