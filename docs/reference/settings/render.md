# Settings: render

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

1 setting in the `render` namespace. See the [settings reference index](README.md).

<a id="render.video.scaling_filter"></a>

## `render.video.scaling_filter`

Filter that the desktop video presenter uses when it scales the luma plane to the window. Chroma planes are always upsampled bilinearly, honouring the stream's chroma siting. ADR-0030 fixes video presentation at VSYNC, so there is no vsync key.

| Field | Value |
|-------|-------|
| Type | `enum` |
| Default | `"linear"` |
| Platforms | Linux, macOS |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

| Value | Meaning |
|-------|---------|
| `linear` | Bilinear interpolation. Smooth scaling at any window size. |
| `nearest` | Nearest-neighbour sampling. Hard pixel edges, useful for pixel-exact inspection. |
