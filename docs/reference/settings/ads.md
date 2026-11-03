# Settings: ads

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

3 settings in the `ads` namespace. See the [settings reference index](README.md).

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
