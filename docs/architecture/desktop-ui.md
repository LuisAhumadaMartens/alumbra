# Desktop UI renderer

[ADR-0030](../adr/0030-desktop-graphics-audio-and-ui-renderer.md) gives the desktop port its own UI renderer. This page
describes what #98 builds: a renderer that draws the [headless UI machine](ui.md)'s ViewModel as an overlay on the
video plane, on the same SDL_GPU device as video ([desktop video output](desktop-video.md)). Code comments are not used
in Alumbra, so these rules live here.

The code is in `antumbra/desktop_ui/` (target `alumbra::desktop_ui`, namespace `alumbra::desktop_ui`). It links
`umbra::ui`, the design tokens and the desktop port, so Umbra stays free of platform headers. It is built when
`ALUMBRA_PORT` is `desktop` and `ALUMBRA_BUILD_UI` is on.

## Frame flow

```text
 SDL event ─► InputTranslator ─► UiMachine::Dispatch ─► effects ─► EffectSink (player, window)
                                        │
                                        ▼ TakeSnapshot() at frame start
 ViewModel ─► ComputeLayout ─► Compose ─► DrawList ─► UiGpuRenderer ─► GpuPresenter overlay pass
   tokens ──────────┘             ▲
   playhead, hover, fade, time ───┘
```

- `DesktopUi::Frame(now_us)` fires due timers, takes the latest snapshot, computes the layout in logical pixels,
  composes the draw list and hands it to the GPU renderer. It returns whether anything still animates, so a frame loop
  can sleep when the video is paused and nothing moves.
- `Compose` is a pure function of the ViewModel, the layout, the tokens, the frame time, the pointer's hover and
  press state and the chrome's fade value. It reads no clock and no GPU state, so its output is deterministic.
- The playhead is extrapolated per frame with `position + rate × (t − anchor)`, clamped to the seekable window,
  as ADR-0028 specifies. The seek bar therefore moves smoothly at 120 and 144 Hz without a snapshot per refresh.
- `GpuPresenter::SetOverlay` takes a `GpuOverlay`. The presenter calls `Prepare` after it has acquired the target and
  before the render pass, for uploads, and `Draw` inside the same render pass after the video, so the order is video,
  then UI. Captions (#140) and the developer overlay will slot between and after them.

## Draw list

The draw list is backend-neutral. Its primitives are rounded rectangles with an optional border, SDF icons, glyphs
and push and pop of a clip rectangle. Coordinates are logical pixels; the GPU renderer multiplies by the window's pixel
density. `DrawList::Describe()` prints it as text, which the tests use for determinism.

Images are in ADR-0030's list of primitives but M1 draws none, so there is no image command yet.

## Text

- `TextShaper` loads the bundled font into HarfBuzz and shapes a string into glyph indices with advances in em units.
  It turns on `tnum`, so digits have equal widths and the clock does not jitter. A label is shaped as one run in the
  catalogue's direction, which is how the mirrored pseudo-locale works without a bidi library (ADR-0030).
- `FontFace` loads the same bytes into FreeType and rasterises one glyph at a pixel size, unhinted, so that glyph
  shapes and HarfBuzz advances agree.
- `GlyphAtlas` packs those bitmaps into a 1024 by 1024 `R8` atlas on the CPU with shelf packing and tracks a dirty flag.
  The GPU renderer uploads it when it changes. A glyph is keyed by index and pixel size, where the pixel size is the
  logical size times the pixel density. When the atlas is full the renderer resets it and rebuilds the frame once.
- Glyph origins are rounded to whole device pixels.

The font is Inter Regular 4.1 under the SIL Open Font License, embedded at build time by
`cmake/AlumbraEmbedBlob.cmake`. The token `typography.font_family.ui` is `system-ui`, which the desktop renderer maps to
the bundled font. All three weight tokens use the one regular file; a variable font is a follow-up.

## Icons

`IconSdfAtlas` builds a signed distance field for each icon on the CPU at start-up from polygons, capsules, circles and
rings on a 24-unit grid, into a 256 by 256 `R8` texture of 64 pixel cells. The fragment shader anti-aliases the edge
from the field's screen-space derivative, so an icon is sharp at any size and pixel density. `IconFromToken` maps the
ViewModel's `icon_token` strings (`icon.play`, `icon.pause`, `icon.volume_on`, `icon.volume_off`,
`icon.fullscreen_enter`, `icon.fullscreen_exit`, `icon.settings`, `icon.skip_ad`) and falls back to a dot. This is a
minimal stand-in for the shared icon set (#336).

## Shaders

`penumbra/desktop/video/shaders/ui.vert.hlsl` and `ui.frag.hlsl` go through the same SDL_shadercross pipeline and drift
check as the video shaders ([Shaders](desktop-video.md#shaders)). One pipeline draws everything. Each vertex carries a
position, a texture coordinate, a colour, the shape (half size, radius, border width), a border colour, a local
position and a kind. The kind selects a rounded box (a signed-distance rectangle with a one pixel anti-aliased edge and
an inner border), a glyph (coverage from the glyph atlas) or an icon (coverage from the icon field). The output is
premultiplied alpha, and the blend is `ONE`, `ONE_MINUS_SRC_ALPHA`. Colours are the tokens' sRGB values, written
to the encoded `SDR` target like the video, with no conversion. Clip rectangles are scissor rectangles, so the draw
list is split into one draw call per clip change.

## Layout

`ComputeLayout` places the controls from the ViewModel and the tokens alone, in logical pixels:

- The control band sits at the bottom, with the seek bar in its top row and the controls below it. Every row is at
  least `target.minimum` high.
- During an ad break, the rectangle above the band is reserved for provider UI. The layout exposes that rectangle,
  pointer input there is not translated into player input, and its overlap check verifies that each visible control
  stays clear. The ad position and countdown occupy the otherwise unused top row of the band.
- Leading controls run left to right from `spacing.medium`; trailing controls are packed against the right edge. The
  control bar does not mirror in right-to-left, following ui.md.
- The volume slider is three targets wide. The clock reserves the width of its widest text. When the leading controls
  do not fit, the layout drops the volume slider first, then the clock.
- The error panel is centred in the area above the band. Its icon and text are aligned to the start edge, which is the
  right edge in the mirrored pseudo-locale.
- The spinner is eight dots on a circle at the centre of the window.

## States

| State | What is drawn |
|-------|---------------|
| Playing, paused, ended | The band, with buffered ranges, the played part and a thumb on hover, focus or scrub; play or pause, mute and fullscreen buttons, the volume slider and the clock |
| Buffering, loading | The spinner after `ui.spinner.delay_ms`, plus the controls |
| Error | The error panel with its icon, title and message from the catalogue, plus the controls |
| Controls hidden | Nothing, so the video is untouched |

The chrome fades with `motion.duration.medium` and `motion.easing.standard`, and the spinner turns once every
three `motion.duration.long`. In reduced motion mode those durations are zero, so the chrome appears and disappears
at once and the spinner stays still.

## Styling and the literal lint

`StyleOf` is the one place that reads `alumbra::design_tokens`. Everything the renderer draws takes its colours,
spacing, radii, type sizes, durations and easing from there. Hover, pressed, focus and disabled states use the
`player.button.*` token states, the focus ring uses `player.focus.ring` and the seek bar uses `player.seekbar.*`.
`DesktopUiOptions` takes a token context (theme, contrast, density, motion) and overrides, so changing a token value
and regenerating changes the screenshot with no renderer change. The tests prove this with an override of
`player.seekbar.played`.

`tools/ui/check_literals.py` fails when `layout.cc`, `compose.cc`, `style.cc` or `animation.cc` contain a hexadecimal
or colour literal, a colour built from numbers, or a number other than 0, 1, 2, 3, 0.5 and 2.0 (as ratios and
structure, not sizes). Counts that are not styles, such as the spinner's dot count and the atlas sizes, are in
`structure.h`. CTest runs the lint as `desktop_ui_literal_lint`, and `desktop_ui_literal_lint.unit` tests the lint.

## Input

`InputTranslator` normalises SDL events into ADR-0028 inputs. Coordinates are logical window coordinates, so HiDPI
needs no conversion.

| Event | Input |
|-------|-------|
| Mouse motion | `PointerActivity`, and `PointerEntered` or `PointerLeft` when the pointer crosses the band |
| Left button down on a button | Records the press; the release over the same button sends `Activate` |
| Left button on the seek bar | `ScrubBegin`, then `ScrubMove` on motion and `ScrubEnd` on release |
| Left button on the volume slider | `SetVolume` on press and on motion |
| Left click on the video | `TogglePlay` |
| Mouse wheel | `VolumeSteps` |
| Key down | The shared keyboard map (`KeyToIntent`) from the SDL keycode; keys with Ctrl, Alt or Cmd are left to the app |
| Window fullscreen events | `PresentationChanged` |

`DesktopUi` also keeps the machine's timers: it stores each `StartTimer` effect and fires it from `Frame` when due,
and `NextTimerDeadlineUs` tells a frame loop when to wake. A `RequestPresentation` effect calls
`SDL_SetWindowFullscreen` when the options name a window. All other effects go to the `EffectSink`, which is how the
player is driven until the effect driver of ui.md's follow-ups exists.

## Accessibility

- A visible focus ring is drawn around the focused control with `player.focus.ring`, offset from it by a gap.
- Every control is operable from the keyboard through the shared map.
- High contrast and reduced motion are token contexts. SDL3 exposes only the system theme, so the port reads no
  contrast or motion preference yet: the app sets the context.
- There is no screen-reader tree on desktop; that is #375. The desktop compatibility matrix must keep saying so.

## Strings and pseudo-locales

`StringCatalogue` is a minimal stand-in for the string catalogue (#335). It holds the English strings the renderer
needs and two pseudo-locales: accented and expanded (vowels get accents and the text is longer and bracketed, with placeholders kept) and mirrored, which
shapes labels right to left and aligns them to the right. The clock is built from digits and always runs left to right.

## Tests

| Target | Covers |
|--------|--------|
| `desktop_ui_unit_test` | Shaping (kerning, tabular digits, right-to-left order), the glyph atlas, icons, strings, easing and fades, layout invariants (minimum targets, no overlap, the clock and volume dropping, no mirroring, token density), the draw list's determinism and the extrapolated playhead at 144 Hz. No GPU |
| `desktop_ui_golden_test` (label `golden`) | Golden screenshots through the real SDL_GPU path (Metal on macOS, lavapipe on Linux CI) for playing with controls, paused, buffering, error, controls hidden, the focus ring, high contrast and both pseudo-locales; a token override; reduced motion; HiDPI; resource leak counters; and injected SDL keyboard and mouse events that drive play, pause, seek and volume |
| `desktop_ui_literal_lint`, `desktop_ui_literal_lint.unit` | The literal-value lint and its tests |

The goldens are binary PPM files in `test/antumbra/desktop_ui/golden/`, 480 by 300 pixels over a gradient video
frame. Comparison is calibrated for each path as ADR-0021 requires: a pixel counts as different when any channel
differs by more than 24, the image fails when more than 0.4 % of its pixels differ or the mean channel difference is
above 0.5. These numbers are #98's calibration for Metal and lavapipe, not a project-wide threshold. The references
are rendered on macOS with Metal. To update them after an intended change, run the test with
`ALUMBRA_UI_GOLDEN_UPDATE=1` and review the files; a failing run writes the actual image next to the build tree under
`test/antumbra/desktop_ui/actual/`. Like the video tests, the GPU tests skip when no device exists unless
`ALUMBRA_REQUIRE_GPU=1`.

## Stubs and follow-ups

- The shared icon set (#336) and the string catalogue (#335) are the minimal stand-ins above.
- The settings menu (#160), captions (#140), ad chrome (#164) and image primitives are not drawn.
- The Dear ImGui developer overlay is out of scope here; it needs a vcpkg review of its own and a place in the
  composition order after the UI.
- Reading the operating system's contrast and motion preferences, a variable font for the weight tokens, and a
  tooltip layer for the control labels.
- The frame loop that owns the window thread (#90) and the effect driver of ui.md.
