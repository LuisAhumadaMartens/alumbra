# 0030 — Build the desktop port on SDL3 and SDL_GPU, with Alumbra's own UI renderer and a replaceable presenter

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #19

## Context

D-19 chooses the window, GPU presentation, audio output and UI-drawing stack of the desktop reference port on macOS
and Linux. Desktop is the reference port ([ADR-0006](0006-platform-order.md)), so its Penumbra implementation
defines what "correct" means for every later port: frame timing, colour conversion, A/V sync and the reference
rendering of the headless UI's ViewModel. Smoothness at high refresh rates is won or lost here. The maintainer's
priorities for this decision are smooth playback at 120 Hz, 144 Hz and above, and quick development. D-19 blocks
desktop video output (#68), the desktop audio sink (#69) and the desktop UI renderer (#98).

Facts about the platform at the time of this decision:

- **SDL3** is zlib-licensed. The pinned vcpkg baseline has `sdl3` 3.4.18, whose licence expression
  (`Zlib AND MIT AND Apache-2.0 AND BSD-3-Clause`) is entirely on [ADR-0014](0014-dependency-and-licensing-policy.md)'s
  automatic list. The manifest's `desktop` feature already includes it.
- **SDL_GPU** targets Metal, Vulkan and Direct3D 12, not OpenGL, which is deprecated on macOS. It offers the
  present modes `VSYNC`, `IMMEDIATE` and `MAILBOX`, and the swapchain compositions `SDR`, `SDR_LINEAR`,
  `HDR_EXTENDED_LINEAR` and `HDR10_ST2084`. It needs shaders for each backend (SPIR-V, MSL, DXIL), allows one to
  three frames in flight (two by default), and exposes no vsync or present timestamps. The `SDL_gpu.h` checked for
  this decision has no function that wraps an existing platform texture or buffer as an `SDL_GPUTexture`.
- **SDL_Renderer** accepts NV12 and P010 textures with colour-space properties, and SDL's `testffmpeg.c` shows it
  wrapping hardware frames without a copy. It gives less control over conversion and pacing than SDL_GPU.
- **Linux CI has no GPU.** SDL's Vulkan backend enumerates software drivers such as Mesa's lavapipe by default.
- **The pinned `sdl3` port is not yet configured for the desktop.** At the pinned baseline, the port's default
  features are `ibus`, `wayland` and `x11` on Linux. The manifest disables them, so Linux builds have neither
  Wayland nor X11 support, and the port's `vulkan` feature, which SDL_GPU's Vulkan backend needs, is off too. The port
  also turns off SDL's PipeWire, PulseAudio, JACK and sndio audio backends, X11's XRandR and the libdecor library
  for Wayland window decorations, and offers only an `alsa` feature for Linux audio. Without XRandR, SDL's X11
  backend reports one generic display with no refresh rate.

Three neighbouring decisions ask D-19 for specific things:

- **D-02** ([ADR-0024](0024-media-clock-and-av-sync.md), #2) makes display resample an opt-in desktop setting from
  M1. It needs a measured refresh interval and the actual present time of each frame, which SDL_GPU does not
  provide, so D-19 must supply desktop present and vsync timing. ADR-0024 also leaves the opt-in desktop
  refresh-rate matching to D-19.
- **D-28** ([ADR-0028](0028-headless-ui-state-machine.md), #28) publishes immutable ViewModel snapshots at most once
  per display frame, with a playhead that renderers extrapolate to each frame's target presentation time. It asks
  D-19 for the desktop renderer's frame callback, and for a platform accessibility tree or the milestone that adds
  one.
- **D-06** ([ADR-0029](0029-decoder-selection-and-fallback.md), #6) keeps desktop M1 on software decoding and adds
  hardware decoding right after M1. Hardware frames reach the renderer as GPU surfaces, without a copy, so D-19 must
  design its video path for GPU-surface frames from the start and verify zero-copy import before hardware decoding
  starts.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **SDL3 on desktop**, with video drawn to textures rather than a separate hardware plane
  ([ADR-0002](0002-portable-engine-porting-layer-html5-api.md), ADR-0006 and
  [ADR-0016](0016-penumbra-port-contract.md)'s texture output mode).
- **Real-time paths** ([ADR-0015](0015-threading-and-task-runners.md)). The vsync thread pulls the due frame with
  `VideoFrameSource::TakeDueFrame`. The audio callback reads a lock-free single-producer ring through
  `AudioSource::Read`, and latency is reported through `AudioSink::GetOutputLatency`
  ([port contract](../architecture/port-contract.md)). Nothing is capped at 60 Hz.
- **Tokens and font** ([ADR-0019](0019-design-tokens.md)). One bundled OFL font, tokens generated as C++
  `constexpr` tables, high-contrast and reduced-motion modes, and an HDR UI reference white of 203 cd/m² set through
  the dynamic-range mode.
- **Composition** (ADR-0016). D-19 and D-23 (#23) own the composition order of the UI and ad overlays.
- **Licences** (ADR-0014). No GPL dependencies, and LGPLv3, which open-source Qt 6 uses, is denied.

## Decision drivers

- Smooth playback at 60 Hz, 120 Hz, 144 Hz and above: honest drop counts, a correct cadence at any refresh rate and
  no 60 Hz cap.
- Quick development: the shortest route to First Light, with the least code to own.
- Exact control of colour conversion, and one rendering path from M1 to HDR output in M11.
- A UI renderer that is a pure, deterministic function of the ViewModel and tokens, so screenshot parity tests are
  meaningful.
- CI tests the code path users run, without a GPU.
- Hardware decoding after M1 lands without a renderer redesign.
- Dependencies within ADR-0014, and Windows (D-35, #35) kept open.
- A measured way out if SDL's abstractions fall short, without rewriting the port.

## Considered options

The definition has nine questions. The issue's option D, Qt 6, is not considered further: its open-source licence is
LGPLv3, which ADR-0014 denies, and using it would need a new ADR.

The desktop stack:

1. **A — SDL3 plus Alumbra's own UI renderer.** SDL3 provides the window, input and audio. SDL_GPU presents video
   through Alumbra's YUV shaders, and a small renderer written for Alumbra draws the UI. Dear ImGui draws only the
   developer overlay.
2. **B — SDL3 plus RmlUi.** The product UI is written in RmlUi's RML and RCSS (MIT).
3. **C — GLFW, native GPU APIs and miniaudio.** GLFW for windows, Metal and Vulkan written directly, and miniaudio
   for audio.

When SDL_GPU arrives:

1. **A — SDL_GPU from the start.**
2. **B — SDL_Renderer first**, and SDL_GPU when HDR arrives.
3. **C — A spike first**, measuring pacing at 120 Hz and 144 Hz and hardware-surface import on both APIs.

Shader language and toolchain:

1. **A — HLSL through SDL_shadercross**, compiled at build time to SPIR-V and MSL.
2. **B — GLSL to SPIR-V to MSL** with glslang and SPIRV-Cross.
3. **C — Hand-written shaders for each backend.**

Present mode and refresh-rate changes:

1. **A — `VSYNC`, following the display's refresh rate.**
2. **B — `MAILBOX` with Alumbra's own pacing.**
3. **C — `VSYNC` plus automatic display-mode switching** to match the content from M1.

Audio position, latency and the trigger for native sinks:

1. **A — An SDL3 pull stream, with native sinks as a measured contingency.**
2. **B — Native sinks from the start:** CoreAudio and PipeWire.
3. **C — miniaudio.**

Bidi and line breaking in M1:

1. **A — No library in M1.**
2. **B — SheenBidi and libunibreak now.**
3. **C — ICU.**

Captions:

1. **A — A separate caption layer** between the video and the controls.
2. **B — Captions as part of the UI ViewModel.**
3. **C — Captions burned into the video texture.**

Linux graphics floor and CI rendering:

1. **A — SDL_GPU's own Vulkan floor, with Mesa's lavapipe in CI.**
2. **B — A Vulkan 1.3 floor.**
3. **C — SDL_Renderer's software path in CI.**

Desktop accessibility in M1:

1. **A — Keyboard operation, a focus ring and the token modes.**
2. **B — Out of scope for M1.**
3. **C — A screen-reader tree now**, for example through AccessKit.

## Decision

Chosen options: **A for all nine questions, with video presentation behind a narrow presenter interface** that a
native Metal or Vulkan presenter can replace. SDL3 and SDL_GPU give the quickest route to First Light with one GPU
API for Metal and Vulkan and a direct path to HDR. Alumbra's own renderer maps 1:1 onto the ViewModel with the least
code to own. The presenter boundary keeps a measured way out, because SDL_GPU's known gaps, no present feedback and no
import of platform textures, are exactly where a native presenter would be stronger. Type and member names below are
illustrative; #68, #69 and #98 fix them.

### Stack

- SDL3 provides the window, input, display information and audio output.
- SDL_GPU presents video through Alumbra's YUV-to-RGB shaders on Metal (macOS) and Vulkan (Linux).
- Alumbra's own renderer draws the product UI on the same GPU device.
- Dear ImGui (MIT) draws only the developer and statistics overlay, the debug overlay of
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md) (#126). It never appears in product screenshot tests.
- SDL_Renderer is not used for product rendering, and there is no fallback renderer. A machine without SDL_GPU
  support cannot open the desktop video output, which reports the failure. SDL_GPU on macOS needs an Apple silicon or
  Mac2-family GPU. On Linux, SDL enumerates software Vulkan drivers by default, so a machine without a hardware
  driver still runs, slowly, through lavapipe.

### Presenter boundary

- Video presentation sits behind a narrow presenter interface inside the desktop port. It is an implementation
  detail of the port's video sink, not part of the Penumbra interface, so it does not change `penumbra::kApiVersion`.
- The presenter owns the swapchain, colour conversion, composition of the layers and present timing. It accepts, for
  each refresh:
  - a video frame as CPU planes or as a GPU surface, with its colour description;
  - a caption layer;
  - the UI draw list described below;
  - the developer overlay.
- It reports the display's refresh rate and its changes, present timing where the platform provides it, and its
  capabilities: which surface types it imports without a copy, and which swapchain compositions the display
  supports.
- M1 builds one presenter, on SDL_GPU. A native presenter, Metal on macOS or Vulkan on Linux, replaces it on that
  platform only if #93's measurements show a pacing gap, or if zero-copy import of hardware-decoded frames
  (VideoToolbox `CVPixelBuffer`s, VA-API surfaces) into SDL_GPU fails.
- Both presenters run the same generated shaders (MSL on Metal, SPIR-V on Vulkan) and execute the same draw list, so
  a native presenter replaces only the backend calls.

The comparison behind this contingency, for macOS:

- **Image quality is identical.** SDL_GPU on macOS is a thin layer over Metal, and both presenters run the same
  shaders into the same swapchain formats.
- **Normal pacing is effectively the same.** Both present with vsync through the same `CAMetalLayer`.
- **A native Metal presenter adds** per-frame presented-time feedback (`MTLDrawable.presentedTime` and presented
  handlers), present-at-time (`MTLCommandBuffer.present(_:atTime:)`), straightforward zero-copy import of
  VideoToolbox frames (`CVMetalTextureCache`), and finer control of extended dynamic range (EDR) on the layer.
- **Its cost** is two GPU backends to write and maintain instead of one, and a third if Windows needs one.

### Video path and colour

- SDL_GPU is used from the start. Video, captions, UI and the developer overlay draw on one device.
- **CPU frames (M1).** Software decoders (#85, #86) produce I420, NV12 and P010 frames. Each plane is uploaded as its
  own one- or two-channel texture (`R8`, `R8G8`, `R16` or `R16G16`) through transfer buffers.
- **Conversion.** Alumbra's shaders convert YUV to RGB with the stream's matrix (BT.601, BT.709 or BT.2020
  non-constant luminance), its range (limited or full) and its chroma siting, then scale to the window at the display
  aspect ratio. #51's `ColorSpace` carries primaries, transfer, matrix and range but not chroma siting yet, so #68
  adds it.
- **GPU-surface frames.** The presenter's frame input accepts `PixelFormat::kOpaque` frames, whose port-owned storage
  holds a platform surface, from the start ([media types](../architecture/media-types.md#decoded-output)). An import
  step for each backend turns the surface into textures, and the same conversion shaders run on them. M1 has no
  hardware decoder, so M1 exercises this path only with test surfaces.
- **Zero-copy import is unproven on SDL_GPU.** VideoToolbox frames would be imported as Metal textures, and VA-API
  surfaces exported as dma-bufs and imported as Vulkan images (`VK_EXT_external_memory_dma_buf` with DRM format
  modifiers). Verifying this without a copy is a prerequisite task before desktop hardware decoding (ADR-0029). If
  SDL_GPU cannot do it, the native presenter for that platform replaces SDL_GPU's before hardware decoding ships.
- **HDR.** M1 presents with the `SDR` composition. The `HDR10_ST2084` and `HDR_EXTENDED_LINEAR` compositions are the
  path to HDR output in M11 (#261), where the caption layer and the UI are composited at ADR-0019's reference white.

### Shaders

- Shaders are written in HLSL and compiled at build time by SDL_shadercross to SPIR-V for Vulkan and MSL for Metal,
  and to DXIL if Windows arrives.
- The compiled shaders are build outputs embedded in the port's binary. They are generated, never edited by hand.
- SDL_shadercross, DXC and SPIRV-Cross are build tools only. Nothing compiles shaders at run time, and DXC is not
  shipped.
- vcpkg's `sdl3-shadercross` port is still at 3.0.0-preview2 at the pinned baseline. The full-SHA baseline
  (ADR-0014) holds it, and each update is reviewed.

### Present mode, frames in flight and refresh rate

- Video always presents with `VSYNC` (FIFO). `MAILBOX` is never used for video, because it replaces a pending image
  without notice, so drop counts would lie. `IMMEDIATE` can tear and is not used for playback. #68's proposed
  `render.video.vsync` toggle is therefore not added.
- Frames in flight stay at the minimum that sustains the display's refresh rate. SDL allows one to three and defaults
  to two; #68 measures whether one holds at 144 Hz and above.
- The refresh rate is read for the window's current display as a rational (`SDL_GetCurrentDisplayMode`), and re-read
  when the window moves to another display (`SDL_EVENT_WINDOW_DISPLAY_CHANGED`) or the display mode changes
  (`SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED`). It is reported to the frame source as ADR-0024 requires.
- Nothing is capped at 60 Hz: the frame loop, UI animation and the extrapolated playhead all run at the display's
  rate.
- Fullscreen switching to a display mode that matches the content comes later as an opt-in setting, using the one
  shared, off-by-default key that ADR-0024 calls for. It is not part of M1.

### Vsync timing for display resample

- SDL_GPU exposes no vsync or present timestamps, so the timing that ADR-0024's display resample needs comes from the
  platform, alongside SDL:
  - **macOS:** `CAMetalDisplayLink` (macOS 14 and later).
  - **Linux on Wayland:** the presentation-time protocol (`wp_presentation`) on the window's surface.
  - **Linux on X11:** the Present extension's completion events.
- The timing source supplies the measured refresh interval, the actual present time of each frame and the deadline
  interval of each refresh. The frame source uses the deadline interval to pick the frame (ADR-0024), and the UI
  renderer uses its target presentation time to extrapolate the playhead (ADR-0028).
- Where no timing source is available, display resample falls back to audio master and reports why, as ADR-0024
  specifies. Examples are macOS before 14, a compositor without `wp_presentation`, an X server without Present, and
  lavapipe in CI.
- The same timing attributes present-late drops, which ADR-0029 reports for the renderer.
- #68 verifies that each timing source works next to SDL_GPU's own swapchain. A conflict counts as a pacing gap for
  the presenter contingency above.

### Frame loop

This is the desktop renderer's frame callback that ADR-0028 asks for. Once per refresh, on the thread that owns the
window (SDL requires swapchain acquisition there), the loop:

1. takes the latest ViewModel snapshot;
2. hands the frame source the refresh's deadline interval and takes the due frame;
3. builds the UI draw list, with the playhead extrapolated to the frame's target presentation time;
4. records the video, caption, UI and developer-overlay passes;
5. submits and presents.

When video is paused and nothing animates, the loop waits for ADR-0028's frame request or for input instead of
redrawing every refresh. #68 and #98 define how the loop shares the window thread with the embedder's runner, so that
a swapchain wait never delays queued tasks (ADR-0015).

### Audio sink

- The sink is an SDL3 audio stream with a get-callback (`SDL_SetAudioStreamGetCallback`) that reads
  `AudioSource::Read` into the stream on SDL's audio thread. Float32 interleaved is the preferred format (#69).
  ADR-0015's real-time rules apply to the callback.
- The played position is the frames delivered, minus the device buffer (`SDL_GetAudioDeviceFormat`), minus the
  latency estimate. `GetOutputLatency` reports the estimate. The reference player smooths the position as ADR-0024
  specifies.
- #69's proposed `audio.output.buffer_frames` keeps SDL's device default until #93's measurements justify another
  value.
- **Native sinks are a measured contingency.** CoreAudio on macOS and PipeWire on Linux replace the SDL3 sink behind
  the same `AudioSink` interface only if #93 misses ADR-0024's failure threshold (outside EBU R37's +40/−60 ms) or
  if position jitter destabilises frame selection.
- miniaudio is not adopted.

### UI renderer and text

- The renderer is a pure function from the ViewModel snapshot, the tokens, the window's pixel density and the
  extrapolated playhead to a backend-neutral draw list. Its primitives are rounded rectangles, SDF icons, text runs
  from a glyph atlas, images and clip rectangles.
- Styling comes only from the generated C++ tokens (ADR-0019, #59).
- Text is shaped by HarfBuzz and rasterised by FreeType into a GPU glyph atlas, from the bundled OFL font, at the
  window's pixel density.
- Menus, the focus ring and scrolling lists are built from these primitives by hand.
- Determinism of the draw list makes screenshot parity tests meaningful (#98, ADR-0021).

### Bidi and line breaking

- M1 adds no bidi or line-breaking library. Its UI is English, left to right, with short labels.
- ADR-0028's mirrored right-to-left pseudo-locale still runs in M1. The renderer mirrors the layout and shapes each
  label as a single run in the paragraph's direction, which HarfBuzz supports without a bidi library. Mixed-direction
  text needs a bidi library.
- When the first right-to-left locale arrives, the renderer reuses FriBidi if D-15 (#15) puts libass, which requires
  FriBidi, on desktop. Otherwise it uses SheenBidi (Apache-2.0) with libunibreak. Alumbra never ships two bidi
  libraries.

### Captions

- Captions get their own layer, drawn between the video and the controls, following the
  [overlay stack](../architecture/overview.md#6-ads-and-the-overlay-stack). The draw order is video, captions,
  controls and the developer overlay. D-23 (#23) places ad overlays.
- The caption layer shares the UI's text engine. ASS subtitles arrive as libass bitmaps uploaded as textures.
- HDR reference white is applied once, to the caption layer and the UI together.
- D-15 (#15) finalises the details in M3.

### Linux graphics and CI

- Linux has no Vulkan floor stricter than SDL_GPU's own: Vulkan 1.0 with `VK_KHR_swapchain`, `VK_KHR_maintenance1`
  and the device features SDL lists.
- CI renders golden frames and screenshots with Mesa's lavapipe, through the same SDL_GPU code path users run, using
  offscreen targets and readback (#68).
- Lavapipe is slow, and its output is not bit-identical to GPUs, so comparisons are calibrated for each path, as
  ADR-0021 requires.
- macOS screenshot runs need a runner with a Metal device. Where a runner has none, the missing evidence is reported
  as a gap under ADR-0021, not covered by lavapipe results.

### Accessibility

- The M1 desktop minimum is full keyboard operation through ADR-0028's shared keyboard map, a visible focus ring
  drawn by the renderer in the `focus` token state, and ADR-0019's high-contrast and reduced-motion modes. As on the
  web, the modes follow the operating system's preference where the port can read it, unless the app sets a mode.
- There is no screen-reader tree on desktop yet, so VoiceOver and Orca users are not supported there. The
  compatibility matrix must say so. Adding a platform accessibility tree is a follow-up with no milestone fixed here.

### Windows (D-35)

The issue's question "Which choices here constrain D-35?" is answered by two choices. HLSL through SDL_shadercross
also emits DXIL, and SDL_GPU's Direct3D 12 backend runs the same port code on Windows. Windows present timing for
display resample is D-35's, as ADR-0024 records.

### Licences and corrections to the issue

- **Qt 6** (option D) uses LGPLv3, which ADR-0014 denies. It would need a new ADR.
- **HarfBuzz** is adopted by this ADR. Its vcpkg licence expression is `MIT-Modern-Variant AND MIT`, and
  `MIT-Modern-Variant` is not on ADR-0014's automatic list. A recorded ADR-0014 review must be accepted before
  HarfBuzz enters the manifest; it is a follow-up below.
- **FreeType**'s expression is `FTL OR GPL-2.0-or-later`. ADR-0014 requires a recorded choice of the FTL branch. The
  port's default features add bzip2 (`bzip2-1.0.6`) and libpng (`libpng-2.0`), neither on the automatic list, so
  they stay off unless reviewed.
- **libunibreak** (`zlib-acknowledgement`) needs a recorded review before the first right-to-left locale adopts it.
  **miniaudio** (`Unlicense OR MIT-0`) is not adopted, and would need a review if that changed.
- **FriBidi.** The issue prefers a permissive bidi library to LGPL FriBidi. ADR-0014 notes that libass cannot be
  built without FriBidi, so if D-15 puts libass on desktop, FriBidi ships anyway and is the bidi library.
- **Build tools.** DXC's vcpkg manifest declares `"license": null` and SPIRV-Cross's declares none, so both need the
  reviewed inventory entries ADR-0014 requires, as build-only tools, before the shader build lands.
- **SDL3, Dear ImGui and SheenBidi** have expressions on the automatic list.
- **No RCSS output.** Option B's RCSS token output is not one of ADR-0019's generated outputs and is not added.
- **No SDL_Renderer fallback.** The issue recommended `SDL_Renderer` as a fallback; it is not used for product
  rendering.

## Consequences

**Positive**
- One GPU path runs from M1 to HDR output in M11, with exact control of colour conversion.
- Drop counts stay honest, and the cadence is correct at 60 Hz, 120 Hz, 144 Hz and above.
- The UI renderer is C++ throughout, maps 1:1 onto the ViewModel and draws deterministic screenshots.
- CI tests the rendering path users run.
- Hardware decoding lands behind the presenter without a renderer redesign, and the contingency for a pacing or
  import gap is a contained backend, not a rewrite.
- Windows stays open through DXIL and SDL_GPU's Direct3D 12 backend.

**Negative / costs**
- A shader toolchain is needed from day one, with a preview `sdl3-shadercross` and DXC in the build.
- Menus, the focus ring, scrolling lists and text layout are built by hand.
- SDL3 provides no presentation timestamps. The audio position is estimated, and vsync timing needs platform code next
  to SDL on each operating system.
- Zero-copy import into SDL_GPU is unproven. If it fails, a native presenter is needed for that platform before
  hardware decoding ships.
- `VSYNC` adds about one frame of input latency compared with `MAILBOX`.
- Desktop has no screen-reader support, no right-to-left locale and no Unicode line breaking in M1.
- Lavapipe is slow and not bit-identical to GPUs, so CI needs calibrated references.
- A machine without SDL_GPU support has no fallback renderer.

**Follow-ups**
- **#68** builds the SDL_GPU presenter behind the presenter interface: plane upload, the conversion shaders, chroma
  siting in `ColorSpace`, `VSYNC` with measured frames in flight, refresh-rate reporting, the platform timing sources
  with their fallback reasons, the frame loop and offscreen golden frames on lavapipe.
- **#69** builds the SDL3 audio sink: the get-callback, position and latency reporting, and route, device and latency
  changes.
- **#68 and #69** configure the `sdl3` port for the desktop: its `vulkan`, `wayland` and `x11` features, X11's
  XRandR so that each display reports its refresh rate, libdecor for Wayland window decorations, and a Linux audio
  backend (the port's `alsa` feature, or PipeWire through an overlay) whose licence is recorded under ADR-0014.
  Where the port has no switch for one of these, an overlay port provides it.
- **#98** builds the UI renderer: the draw list, HarfBuzz and FreeType text in a glyph atlas, SDF icons, the focus
  ring, the shared keyboard map, the token modes, the Dear ImGui developer overlay, and screenshot tests in both of
  ADR-0028's pseudo-locales.
- **#93** measures A/V sync and position jitter on the SDL3 sink and frame pacing on the SDL_GPU presenter. Its
  results decide the native-sink and native-presenter contingencies.
- **Licence reviews** (#40): HarfBuzz's `MIT-Modern-Variant`, FreeType's FTL choice and its features, and DXC and
  SPIRV-Cross as build-only tools, each accepted before the dependency enters the manifest.
- **Zero-copy verification (new task, before desktop hardware decoding):** import VideoToolbox `CVPixelBuffer`s and
  VA-API surfaces, and any other backend on ADR-0029's allowlist, into SDL_GPU without a copy. A failure on a
  platform triggers the native presenter there.
- **D-35 (#35)** uses DXIL, SDL_GPU's Direct3D 12 backend and Windows present timing if Windows ships.
- **D-15 (#15)** finalises the caption layer and libass in M3, and with it whether FriBidi becomes the bidi library.
- **M11 HDR (#261)** adds the `HDR10_ST2084` and `HDR_EXTENDED_LINEAR` compositions, tone mapping and the UI
  reference white on the presenter.
- **Desktop accessibility tree:** `docs/compatibility.md` gains a statement that desktop has no screen-reader support
  until a platform accessibility tree is added. The M11 audit (#271) covers the desktop renderer.
- **Not decided here:** ADR-0024 asked whether variable-refresh-rate displays are driven at a multiple of the content
  rate. That needs #93's measurements on such a display and a later decision.
- **Content-matching display modes** arrive as a later opt-in setting with ADR-0024's shared key.

## Pros and cons of the options

### Desktop stack

- **A. SDL3 plus Alumbra's own UI renderer.** Good, because it maps 1:1 onto the ViewModel, is C++ throughout and
  keeps the dependency set light. Bad, because menus, focus rings and scrolling lists are built by hand.
- **B. SDL3 plus RmlUi.** Good, because there is less widget code and CSS-like styling. Bad, because a DOM-like layer
  sits between the ViewModel and the pixels, it needs an RCSS token output that ADR-0019 does not list, and an
  SDL_GPU render interface must be written.
- **C. GLFW, native GPU and miniaudio.** Good, because it gives full control of display-link timing, HDR metadata and
  present feedback. Bad, because two GPU backends must be written before First Light, and miniaudio's licence needs a
  review.

### When SDL_GPU arrives

- **A. SDL_GPU from the start.** Good, because it gives one path to M11 with exact control of colour, and M1 uploads
  software frames anyway. Bad, because a shader toolchain is needed from day one and zero-copy import must be proven
  later.
- **B. SDL_Renderer first.** Good, because it gives the quickest first pixels and has proven zero-copy hardware
  frames. Bad, because pacing and conversion are less controlled, and the UI and HDR work migrate later.
- **C. A spike first.** Good, because it decides on evidence. Bad, because it delays #68.

### Shader language

- **A. HLSL through SDL_shadercross.** Good, because it is SDL's documented route and already covers Direct3D 12.
  Bad, because the tool is a preview release and DXC joins the build.
- **B. GLSL to SPIR-V to MSL.** Good, because the tools are mature. Bad, because there is no DXIL path for Windows
  without extra steps.
- **C. Hand-written per backend.** Good, because there is no toolchain. Bad, because every change is made twice.

### Present mode

- **A. `VSYNC`, following the display.** Good, because nothing is dropped silently and the cadence is right at any
  refresh rate. Bad, because input latency is about one frame higher than with `MAILBOX`.
- **B. `MAILBOX` with own pacing.** Good, because UI latency is lowest. Bad, because frames can be discarded without
  notice, so drop counts lie.
- **C. `VSYNC` plus automatic mode switching.** Good, because 24p plays without judder. Bad, because monitors blank
  while they resync, and more platform code arrives early.

### Audio sink

- **A. SDL3 pull stream.** Good, because it is the quickest route, with one code path. Bad, because SDL3 exposes no
  presentation timestamp, so the estimate may need replacing.
- **B. Native sinks from the start.** Good, because the clock quality is best. Bad, because two sinks must be built
  before First Light.
- **C. miniaudio.** Good, because it is one portable library. Bad, because its timestamps are no better than SDL3's,
  and its licence needs a review.

### Bidi and line breaking

- **A. None in M1.** Good, because there is no premature dependency and never two bidi libraries. Bad, because
  right-to-left locales wait.
- **B. SheenBidi and libunibreak now.** Good, because right-to-left is ready from M1. Bad, because libunibreak needs a
  licence review and nothing uses either library until localisation.
- **C. ICU.** Good, because Unicode support is complete. Bad, because it is very large and needs a licence review.

### Captions

- **A. A separate caption layer.** Good, because captions scale and stay legible independently of the controls, and
  HDR reference white is applied once. Bad, because it adds a composition pass.
- **B. Part of the UI ViewModel.** Good, because there is one renderer. Bad, because timed media mixes with chrome,
  and caption timing becomes tied to UI updates.
- **C. Burned into the video texture.** Good, because it is simple. Bad, because captions scale with the video, which
  is wrong for HDR and for accessible caption styling.

### Linux graphics and CI

- **A. SDL_GPU's floor, lavapipe in CI.** Good, because CI tests the real path and no GPU is excluded without reason.
  Bad, because lavapipe is slow and needs calibrated comparisons.
- **B. Vulkan 1.3 floor.** Good, because features such as dynamic rendering become available. Bad, because older GPUs
  are excluded for no M1 gain.
- **C. SDL_Renderer software path in CI.** Good, because it is fast. Bad, because CI then tests a different path from
  the one users run.

### Desktop accessibility

- **A. Keyboard, focus ring and token modes.** Good, because it is cheap and uses modes the tokens already define.
  Bad, because screen-reader users are not supported on desktop.
- **B. Out of scope for M1.** Good, because it costs nothing now. Bad, because keyboard paths are retrofitted later.
- **C. Screen-reader tree now.** Good, because accessibility is real from M1. Bad, because AccessKit's C bindings
  bring a Rust toolchain into the build, and M1's scope grows a lot.

## References

- [D-19 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/19)
- SDL3: [GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU) ·
  [SDL_GPUPresentMode](https://wiki.libsdl.org/SDL3/SDL_GPUPresentMode) ·
  [SDL_GPUSwapchainComposition](https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition) ·
  [SDL_SetGPUAllowedFramesInFlight](https://wiki.libsdl.org/SDL3/SDL_SetGPUAllowedFramesInFlight) ·
  [include/SDL3/SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_gpu.h) ·
  [SDL_GetCurrentDisplayMode](https://wiki.libsdl.org/SDL3/SDL_GetCurrentDisplayMode) ·
  [SDL_EventType](https://wiki.libsdl.org/SDL3/SDL_EventType)
- SDL3 audio: [Audio API](https://wiki.libsdl.org/SDL3/CategoryAudio) ·
  [SDL_SetAudioStreamGetCallback](https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback) ·
  [SDL_GetAudioDeviceFormat](https://wiki.libsdl.org/SDL3/SDL_GetAudioDeviceFormat)
- [SDL: test/testffmpeg.c](https://github.com/libsdl-org/SDL/blob/main/test/testffmpeg.c)
- [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) and its
  [vcpkg port at the pinned baseline](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/sdl3-shadercross/vcpkg.json);
  [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- Apple: [CAMetalDisplayLink](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink) ·
  [MTLDrawable presentedTime](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime) ·
  [MTLDrawable addPresentedHandler](https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler(_:)) ·
  [MTLCommandBuffer present(_:atTime:)](https://developer.apple.com/documentation/metal/mtlcommandbuffer/present(_:attime:)) ·
  [CVMetalTextureCache](https://developer.apple.com/documentation/corevideo/cvmetaltexturecache) ·
  [Metal: HDR content](https://developer.apple.com/documentation/metal/hdr-content)
- Wayland: [presentation-time protocol](https://wayland.app/protocols/presentation-time)
  ([protocol XML](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/presentation-time/presentation-time.xml))
- X11: [Present extension protocol](https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/blob/master/presentproto.txt)
- Mesa: [lavapipe source](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/frontends/lavapipe) and
  [llvmpipe, which lavapipe builds on](https://docs.mesa3d.org/drivers/llvmpipe.html)
- Vulkan: [VK_EXT_external_memory_dma_buf](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_external_memory_dma_buf.html);
  [VA-API core API](https://intel.github.io/libva/group__api__core.html)
- Text: [HarfBuzz](https://harfbuzz.github.io/) · [FreeType licence](https://freetype.org/license.html) ·
  [SheenBidi](https://github.com/Tehreer/SheenBidi) · [libunibreak](https://github.com/adah1972/libunibreak) ·
  [FriBidi](https://github.com/fribidi/fribidi) · [libass](https://github.com/libass/libass) ·
  [UAX #9: Unicode Bidirectional Algorithm](https://www.unicode.org/reports/tr9/) ·
  [UAX #14: Unicode Line Breaking Algorithm](https://www.unicode.org/reports/tr14/)
- vcpkg licence metadata at the pinned baseline:
  [harfbuzz](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/harfbuzz/vcpkg.json) ·
  [freetype](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/freetype/vcpkg.json) ·
  [libunibreak](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/libunibreak/vcpkg.json) ·
  [miniaudio](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/miniaudio/vcpkg.json) ·
  [directx-dxc](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/directx-dxc/vcpkg.json) ·
  [spirv-cross](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/spirv-cross/vcpkg.json)
- Other options: [Dear ImGui](https://github.com/ocornut/imgui) · [RmlUi](https://github.com/mikke89/RmlUi) ·
  [GLFW](https://www.glfw.org/) · [miniaudio](https://miniaud.io/) ·
  [Qt: LGPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations) ·
  [AccessKit](https://github.com/AccessKit/accesskit)
- [mpv manual: video-sync](https://mpv.io/manual/stable/#options-video-sync)
- [ITU-R BT.2408](https://www.itu.int/rec/R-REC-BT.2408)
- [ADR-0006](0006-platform-order.md), [ADR-0007](0007-ui-architecture.md),
  [ADR-0014](0014-dependency-and-licensing-policy.md), [ADR-0015](0015-threading-and-task-runners.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0019](0019-design-tokens.md),
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md), [ADR-0021](0021-testing-strategy.md),
  [ADR-0024](0024-media-clock-and-av-sync.md), [ADR-0028](0028-headless-ui-state-machine.md),
  [ADR-0029](0029-decoder-selection-and-fallback.md),
  [port contract](../architecture/port-contract.md), [media types](../architecture/media-types.md)
