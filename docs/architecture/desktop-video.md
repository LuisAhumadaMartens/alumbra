# Desktop video output

[ADR-0030](../adr/0030-desktop-graphics-audio-and-ui-renderer.md) puts the desktop port's video on SDL3 and SDL_GPU
(Metal on macOS, Vulkan on Linux). This page describes what #68 builds: the SDL_GPU presenter behind a narrow
presenter interface, the YUV conversion shaders, the geometry, the timing hooks and the tests. Everything lives in
`penumbra/desktop/`; no SDL header reaches Umbra or the Penumbra interface, and none of it changes
`penumbra::kApiVersion`.

## Layout

| Path | Contents |
|------|----------|
| `penumbra/desktop/internal_include/penumbra/desktop/video/presenter.h` | The presenter interface: `Presenter`, `PresentRequest`, `PresenterStats`, `PresenterCapabilities`, `PresenterObserver`, `RefreshRate`, `GpuSurfaceHandle`. It has no SDL types. |
| `.../gpu_presenter.h`, `video/gpu_presenter.cc` | `GpuPresenter`, the SDL_GPU implementation, with the resource counters used by the leak tests. |
| `.../color_conversion.h` | `ComputeConversionParams`: matrix, range, bit depth and chroma siting as the shader's uniform block. |
| `.../plane_layout.h` | Plane textures of each CPU format (I420, NV12, P010). |
| `.../video_geometry.h` | Display aspect ratio from the sample aspect ratio, letterboxing and pixel density. |
| `.../video_sink.h` | `DesktopVideoSink`, the `VideoSink` implementation that pulls `TakeDueFrame` once per refresh. |
| `.../sdl_session.h` | RAII for SDL's video subsystem and the window. |
| `video/shaders/*.hlsl` | HLSL sources. |
| `video/shaders/generated/` | Generated SPIR-V (`.spv`) and MSL (`.msl`), embedded into the port by CMake. |

## Presenter boundary

`Presenter` is the interface a native Metal or Vulkan presenter would replace. It accepts, for each refresh, a
`PresentRequest` with an optional new frame (CPU planes or a `PixelFormat::kOpaque` GPU surface), the sample aspect
ratio and the bounds inside the target. A request without a frame repeats the last frame. It reports the display's
refresh rate, a `PresenterStats` snapshot, its capabilities and, through `PresenterObserver`, refresh-rate changes
and a `PresentRecord` for every refresh.

`GpuPresenter` has two targets. With a window it claims the window, sets the `SDR` composition and always the
`VSYNC` present mode; `MAILBOX` and `IMMEDIATE` are never used, and there is no `render.video.vsync` key. Without a
window it renders into an offscreen texture whose pixel size is the logical size times the pixel density, and
`ReadbackTarget()` downloads it with `SDL_DownloadFromGPUTexture`. CI uses this path for golden frames.
`frames_in_flight` defaults to 1, the minimum; #93 measures whether one holds at 144 Hz and above.

## Colour conversion

Each plane is uploaded as its own texture (`R8`, `R8G8`, `R16` or `R16G16`) through a transfer buffer, and one
fragment shader converts. For a texture sample `s`, the shader computes `yuv = s * scale + bias` and then
`rgb = M * yuv`:

- `scale` and `bias` come from the range and the bit depth. Limited range maps codes 16 to 235 (luma) and 16 to 240
  (chroma) to 0 to 1, scaled by `2^(depth-8)`; full range uses the whole code range. P010 keeps its 10 bits in the
  high bits of an `R16` sample, so its sample-to-code factor is `65535/64`.
- `M` follows from the luma weights of BT.601, BT.709 or BT.2020 non-constant luminance. The matrix comes from the
  stream's H.273 `matrix` code (1, 5, 6 and 9). Any other code, including 2 (unspecified), picks BT.709 from 1280 x 720
  upwards and BT.601 below it.
- Chroma is upsampled bilinearly with its siting. `ColorSpace` gained `ChromaSiting` (`kUnspecified`, `kLeft`,
  `kCenter`, `kTopLeft`); `kUnspecified` is treated as `kLeft`, the MPEG-2 and H.264 default.
- The output is the swapchain's encoded `SDR` values: there is no transfer-function conversion in M1 (HDR is #261).

The luma plane is sampled with `render.video.scaling_filter` (`linear` or `nearest`); chroma is always bilinear.

## GPU surfaces

`PixelFormat::kOpaque` frames are designed in. By convention, the `storage` of an opaque frame in the desktop port
points at a `GpuSurfaceHandle` (build it with `MakeOpaqueStorage`), whose `Kind()` selects an import path. M1 has one
kind, `kTexturePlanes`, which wraps textures the presenter's own device created (`CreateTestSurface`), and the same
conversion shader runs on them. Hardware decoding is after M1 and adds the VideoToolbox and VA-API kinds; verifying
their zero-copy import is the prerequisite task that ADR-0030 lists.

## Geometry

The display aspect ratio is `width * SAR.num : height * SAR.den`. The video is fitted inside the bounds with
letterboxing or pillarboxing, centred, and drawn with a viewport and scissor so that the rest of the target stays
black. The `OutputBounds` of `VideoSink::SetBounds` are in target pixels, and empty bounds mean the whole target.
With a window, the pixel size comes from the swapchain texture, so HiDPI follows the window's pixel density.
`GpuPresenter::HandleEvent` re-reads the density and the refresh rate on `SDL_EVENT_WINDOW_DISPLAY_CHANGED`,
`SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED` and the pixel-size and scale events.

## Frame counts and timing hooks

`PresenterStats` carries `presented_frames` (new frames shown), `dropped_frames` (frames that failed to upload or
present), `repeated_refreshes` and the acquire and submit times of the last refresh. `DesktopVideoSink::
PlaybackQuality()` exposes the first two for `getVideoPlaybackQuality()`.

ADR-0030 assigns the vsync and present timing for display resample to the desktop port. #68 supplies the hooks:
the refresh rate as a rational (`SDL_GetCurrentDisplayMode`, re-read on change), a `PresentRecord` for each refresh
with the swapchain-acquire and submit times in `SDL_GetTicksNS` units, and `PresenterCapabilities::present_timing`
with a reason string. The platform sources (`CAMetalDisplayLink` and `MTLDrawable` handlers on macOS,
`wp_presentation` on Wayland, the Present extension on X11) are not built yet, so `present_timing` is `kNone` and
the reason says so; the cadence estimator falls back to audio master as ADR-0024 specifies. The frame selection
itself and the clock are #90's.

## Shaders

The HLSL sources are compiled by `tools/shaders/generate.py`, which runs SDL_shadercross (HLSL to SPIR-V with DXC,
then SPIR-V to MSL with SPIRV-Cross). The outputs are generated, never edited by hand, and CMake embeds the files in
`generated/` into the port's binary, so nothing compiles shaders at run time and the build on macOS needs no
shader toolchain. The vcpkg `sdl3-shadercross` port provides the tool, and DXC does not run on macOS, so the port is
a host dependency of `x64-linux` only. On that platform the `shader_outputs_match_sources` test regenerates the
outputs and fails if they differ from the committed files:

```sh
python3 tools/shaders/generate.py \
  --shadercross build/debug/vcpkg_installed/x64-linux/tools/sdl3-shadercross/shadercross \
  --library-path build/debug/vcpkg_installed/x64-linux/lib
```

DXC and SPIRV-Cross are build-only tools and are never shipped; their licence entries are in
[THIRD_PARTY.md](../../THIRD_PARTY.md).

## Dependencies

The `desktop` feature selects the vcpkg `sdl3` port with its `vulkan` feature, plus `wayland` and `x11` on Linux. The
port's stock recipe turns X11's XRandR off, so `cmake/ports/sdl3` overlays it with `SDL_X11_XRANDR=ON`
([overlay ports](../../cmake/ports/README.md)); without XRandR the X11 backend reports no refresh rate. The stock
recipe already sets `SDL_WAYLAND_LIBDECOR=OFF`. Linux builds need the `libx11-dev`, `libxext-dev`, `libxrandr-dev`,
`libwayland-dev`, `wayland-protocols`, `libxkbcommon-dev` and `libegl1-mesa-dev` packages, and the tests need Mesa's
lavapipe (`mesa-vulkan-drivers`, `libvulkan1`) because CI has no GPU.

## Tests

| Target | Covers |
|--------|--------|
| `penumbra_desktop_video_test` | Matrix, range and siting maths, plane layouts, display aspect ratio, letterboxing, pixel density. No GPU. |
| `penumbra_desktop_golden_test` (label `golden`) | Colour bars in BT.601 and BT.709, limited and full range, in I420, NV12 and P010, within +/-4 of the reference RGB. Detailed frames against an independent double-precision CPU reference (max error 4, mean error 1). Chroma siting, anamorphic display aspect ratio, letterboxing, bounds, scaling filter, opaque surfaces, counters, observer records. A 200-step resize and pixel-density stress test with resource counters. |
| `shader_generator.unit`, `shader_outputs_match_sources` | The generator, and the drift check on `x64-linux`. |

The GPU tests run through the same SDL_GPU code path on Metal (macOS) and on lavapipe (Linux, selected through SDL's
`offscreen` video driver). They skip when no device exists, unless `ALUMBRA_REQUIRE_GPU=1`, which CI sets on Linux.
The tolerances cover 8-bit quantisation of the encoded bars and the filtering precision of each GPU. ADR-0021 leaves
numeric image thresholds to per-case review, so these values are #68's calibration for Metal and lavapipe, not a
project-wide threshold. Leak freedom is checked by `GpuResourceCounters` (`Live()`) (every texture, transfer buffer, shader,
pipeline, sampler and fence the presenter creates is counted, and the count must return to zero) and by the ASan
legs.

## Deferred

- The frame loop that shares the window thread with the embedder's runner, the media clock and frame selection: #90.
- The UI, caption and developer-overlay passes of the composition order, and the UI draw list: #98.
- The audio sink: #69.
- The platform vsync and present timing sources, and the `playback.display.rate_match` requests: #379.
- Hardware-decoded surfaces and their zero-copy import: #328 and #341, after M1 (ADR-0029).
- HDR compositions and tone mapping: #261.
