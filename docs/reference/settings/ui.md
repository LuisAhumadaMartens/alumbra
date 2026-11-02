# Settings: ui

> **Generated file. Do not edit.** This page is generated from `settings/schema/*.toml` by `tools/settings/generate.py`. Change the schema and run `python3 tools/settings/generate.py`; hand edits are overwritten and fail the drift check.

7 settings in the `ui` namespace. See the [settings reference index](README.md).

<a id="ui.controls.auto_hide_ms"></a>

## `ui.controls.auto_hide_ms`

Inactivity before the controls hide. The default of 3000 ms follows #97.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `3000` |
| Range | `1` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.controls.hidden"></a>

## `ui.controls.hidden`

Built-in controls to hide. The empty default hides none.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.controls.order"></a>

## `ui.controls.order`

Order of built-in controls within their slots. The empty default keeps the built-in order.

| Field | Value |
|-------|-------|
| Type | `string_list` |
| Default | `[]` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.controls.seek_step_ms"></a>

## `ui.controls.seek_step_ms`

Keyboard and D-pad seek step. The default of 5000 ms follows #97. Ports may supply a different port default.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `5000` |
| Range | `1` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.controls.volume_step"></a>

## `ui.controls.volume_step`

Keyboard volume step as a fraction of full scale. The default of 0.05 follows #97.

| Field | Value |
|-------|-------|
| Type | `double` |
| Default | `0.05` |
| Range | `0.0` to `1.0` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.double_tap_seek.enabled"></a>

## `ui.double_tap_seek.enabled`

Whether a double tap on the left or right half of a touch screen seeks by the seek step. The default is on, offered on touch platforms only, following ADR-0028 and the maintainer default recorded for #97.

| Field | Value |
|-------|-------|
| Type | `boolean` |
| Default | `true` |
| Platforms | Android, iOS, iPadOS |
| Mutability | Live |
| Audience | End user |
| Stability | Experimental |
| Since | 0.1.0 |

<a id="ui.spinner.delay_ms"></a>

## `ui.spinner.delay_ms`

Delay before the buffering spinner appears. The default of 500 ms follows #97.

| Field | Value |
|-------|-------|
| Type | `duration` |
| Default | `500` |
| Range | `0` to `9223372036854775807` |
| Unit | `ms` |
| Platforms | Linux, macOS, Windows, Android, Google TV, iOS, iPadOS, tvOS, Web |
| Mutability | Live |
| Audience | Integrator |
| Stability | Experimental |
| Since | 0.1.0 |
