# Desktop audio output

[ADR-0030](../adr/0030-desktop-graphics-audio-and-ui-renderer.md) puts the desktop port's audio on an SDL3 audio
stream. This page describes what #69 builds: the real-time renderer, the SDL sink around it, the played-position
estimate, route and device changes, the Linux backend and the tests. Everything lives in `penumbra/desktop/audio/`;
no SDL header reaches Umbra or the Penumbra interface, and none of it changes `penumbra::kApiVersion`.

## Layout

| Path | Contents |
|------|----------|
| `penumbra/desktop/internal_include/penumbra/desktop/audio/audio_renderer.h`, `audio/audio_renderer.cc` | `AudioRenderer`: the real-time callback body, gain ramps, pause, flush and the position history. It has no SDL types, so the tests drive it with a fake device. |
| `.../audio_sink.h`, `audio/audio_sink.cc` | `DesktopAudioSink`, the `AudioSink` implementation: device open, the SDL get-callback, latency and route reporting, device events. `AudioSinkObserver`, `AudioRoute` and `AudioPosition` are its value types. |

## Thread model

| Thread | What it does |
|--------|--------------|
| Control | `Start`, `Stop`, `Pause`, `Resume`, `Flush`, `SetVolume`, `SetMuted`, `GetOutputLatency`, `GetPosition`, `ProcessDeviceEvents`. Never called from the audio thread. |
| SDL audio thread | Runs SDL's get-callback (`SDL_OpenAudioDeviceStream` with a callback), which asks `AudioRenderer::Render` for as many frames as SDL needs and passes them to `SDL_PutAudioStreamData`. |

The renderer is the only code on the callback path that Alumbra owns. It takes no lock, allocates nothing and posts no
task, as ADR-0015 requires: control changes reach it through lock-free atomics (`std::atomic<float>`, `bool`,
`int64_t`, asserted `is_always_lock_free`), its scratch buffer is sized at `Start`, and the monotonic clock is read
through a preallocated object. `AudioSource::Read` is the contract's `noexcept`, nonblocking call. `SDL_PutAudioStreamData`
takes SDL's own stream lock and may refill SDL's internal chunk pool; that is SDL's code and is outside Alumbra's
guarantee, which is why the renderer does not call SDL.

The sink always opens the stream as interleaved `float32`, the format ADR-0030 prefers, at the stream's sample rate
and channel count. SDL converts to the device's format, rate and channel layout. `Start` accepts interleaved `F32`,
`S16` and `S32` sources and converts them to `float32` in the renderer; `U8`, planar formats and more than 8 channels
give `kUnimplemented`, an unknown format, a null source or a nonpositive rate or channel count give
`kInvalidArgument`, and a second `Start` or one after `Stop` gives `kFailedPrecondition`. `Stop` is idempotent and,
once it returns, SDL has closed the device, so no callback can read the source.

## Pause, resume, flush, volume and mute

- **Gain ramps.** Volume, mute and pause each move a gain towards its target at one step per frame, so a change takes
  5 ms and never steps. The stream starts with a ramp from silence, and so does the first audio after a flush.
- **Pause** ramps the gain to zero and then stops reading the source: the source keeps its frames, and `Resume` ramps
  back in from the next one. The device keeps running and plays silence, so resuming is immediate. **Mute** only
  ramps the gain; the source keeps being consumed, so media time keeps moving.
- **Flush** is for a seek. It pauses the SDL device, which waits for any iteration in flight (so nothing queued reaches
  the device afterwards), closes a gate that makes the callback output silence without reading the source, waits for
  a callback that already started, clears the SDL stream queue, resets the position history and counters, bumps the
  flush generation and resumes. The caller owns the source, so it must make the source return no data (the reference
  player's generation filter) before `Flush` and give it new data after it returns. Audio that SDL had already
  handed to the device before the pause, at most one device buffer, cannot be recalled.
- **Volume** is a linear factor from 0 to 1; anything else gives `kInvalidArgument`.

## Output latency and played position

`GetOutputLatency` returns the device buffer plus the audio still queued in the SDL stream:
`SDL_GetAudioDeviceFormat` gives the device's buffer in frames and rate, and `SDL_GetAudioStreamQueued` gives the
queue. It is an estimate of SDL's own buffering. SDL has no view of Bluetooth, HDMI or receiver delay, so those
routes are not included; `playback.sync.audio_offset_ms` covers them (ADR-0024). The unknown sentinel is returned
before `Start`, after `Stop` and when no device is open; a failing SDL query is a non-OK status. The latency is
logged at `Info` when the device opens, with the driver, device, format and buffer size.

`GetPosition` returns the media frames delivered and the media frames played. The renderer records, for each callback,
the cumulative device frames (media and silence), the cumulative media frames and the monotonic time. For a query at
time `t` it computes the heard device position `H = S - L + (t - T)`, where `S` and `T` come from the latest record,
`L` is the latency in frames, clamped to `[0, S]`, and maps `H` back to media frames through the records of the last
62 callbacks. Silence from an underrun or a pause is therefore not media time: the position holds while the sink plays
silence, as ADR-0024 requires, and continues from the same frame afterwards. The history is a single-writer ring that
the control thread reads without a lock and retries if the writer advanced into the slots it read.

The estimate is raw. ADR-0024 smooths it, and #90 owns that, together with the clock start rule, the offset and the
slew; this sink hands #90 the delivered frames, the played estimate, the latency and the flush generation.

## Route, device and latency changes

SDL reports device events as `SDL_EVENT_AUDIO_DEVICE_REMOVED` and `SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED`. The sink
registers an event watch that only sets an atomic flag, so it never consumes events that the window loop wants, and
`ProcessDeviceEvents` (control thread, called by the embedder's frame or timer tick) acts on the flags:

- **Format changed**, for example a default-device switch that SDL follows by itself: the sink re-queries the route and
  calls `AudioSinkObserver::OnAudioRouteChanged(kFormatChanged, route)` with the new latency, and logs it.
- **Device removed**, for a named device: the sink closes the stream and reopens on the default device. On success it
  reports `kDeviceChanged` with the new route and logs a warning. On failure it returns
  `kUnavailable` with a message starting `capability changed`, calls `OnAudioError` with a `PortError` of category
  `kCapability` and recovery hint `kRecreate`, and logs an error. The sink then has no device: latency is unknown,
  `GetPosition` keeps working on the frames already delivered, and the embedder recreates the sink. The current
  catalogue has no dedicated capability-changed code ([port contract](port-contract.md#starboard-lesson-coverage-and-reserved-slots)); #332
  assigns it, and this error moves to it then.

With the system default device (`audio.output.device` empty), SDL migrates the stream itself when the default device
changes or disappears, and the sink only sees the format event.

## Settings

| Key | Use |
|-----|-----|
| `audio.output.device` | `AudioSinkOptions::device_name`. Empty opens the system default. A name that no device has gives `kNotFound` at `Start`. The key is in `settings/schema/audio.toml`; #54 resolves it into the options. |
| `playback.sync.audio_offset_ms` | Applied by the media clock (#90, with #327), not by the sink. It replaces #69's proposed `audio.output.latency_compensation_ms`. |

`audio.output.buffer_frames` is not added: the sink keeps SDL's device default until #93's measurements justify
another value, as ADR-0030 says. `AudioSinkOptions::buffer_frames` exists for tests and measurements and sets
`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` when it is nonzero.

## Headless operation and capture

`AudioSinkOptions::driver` sets `SDL_HINT_AUDIO_DRIVER` before the subsystem starts. SDL's `dummy` driver consumes
audio in real time and discards it; the `disk` driver writes raw `float32` frames to the file named by
`SDL_HINT_AUDIO_DISK_OUTPUT_FILE`, at the device's pace scaled by `SDL_HINT_AUDIO_DISK_TIMESCALE`. #93 uses the disk
driver to capture a run for its sync measurement. Neither driver needs a device, so the tests run on CI.

## Linux backend

The manifest adds the `sdl3` port's `alsa` feature on Linux. The stock port turns PipeWire, PulseAudio, JACK and
sndio off and has no switch for them. A PipeWire or PulseAudio overlay is not needed: both serve ALSA clients through
their ALSA plugins, so SDL's ALSA driver reaches either server. ALSA is LGPL-2.1-or-later, so the
Linux triplets build the `alsa` port as a shared library, as they do for FFmpeg, and SDL (a static library) opens
`libasound.so.2` at run time through `SDL_LoadObject` (`SDL_ALSA_SHARED`, the port's default) instead of linking it.
No ALSA code is therefore inside an Alumbra artefact, and the library stays replaceable under ADR-0014. The reviewed
exception is in [`licence_policy.json`](../../tools/ci/licence_policy.json) and the inventory in
[THIRD_PARTY.md](../../THIRD_PARTY.md). The `alsa` port builds with Autotools, so Linux builds also need the
`autoconf`, `autoconf-archive`, `automake` and `libtool` packages. The sanitizer triplets build `alsa` without sanitizer flags, because a shared library does not link the sanitizer runtime. On macOS SDL uses CoreAudio, which is part of the system.

A native CoreAudio or PipeWire sink is only a measured contingency (#93) and is not built.

## Tests

| Target | Covers |
|--------|--------|
| `penumbra_desktop_audio_test`, `AudioRendererTest` | Format validation, fade-in, `S16` conversion, underruns that do not advance media time, pause that stops reading the source and resumes contiguously, ramped volume and mute, flush against a callback in flight (from another thread). |
| `penumbra_desktop_audio_allocation_test` | **No allocation** while a second thread renders and the control thread changes every control: the test replaces the global `operator new` and counts allocations made by the render thread. The sanitizers define their own `operator new`, so this target is built only with `ALUMBRA_SANITIZER=none`. |
| `DeviceModelTest` | The position estimate against a fake device that pulls one period per callback and plays it one period later: within one device buffer period (480 frames) at every step of 200 callbacks, monotonic, held across an underrun, never past the delivered frames. No clock, no SDL. |
| `DesktopAudioSinkTest` | Argument errors, latency reported and logged at open, a capture through SDL's `disk` driver with a counter signal (no gap or repeat), position against the frames the device consumed, pause and resume through the capture, flush (no pre-seek sample after the first post-seek sample), volume and mute, the `dummy` driver, removal of the active device (reopen, and the `kCapability` error when no replacement opens) and format changes. The removal and format events are injected with `SDL_PushEvent`. |

The sink tests use SDL's drivers at a speed of five times real time and poll with a deadline, so a slow runner
makes them slower, not flaky. The real-time check proves that Alumbra's callback code does not allocate; it cannot see
inside SDL, and it does not detect a lock that a future change adds, which the lock-free `static_assert`s and review
cover.

## Deferred

- The media clock: audio as master, the start rule, smoothing of the position, the route-change slew and the
  `playback.sync.audio_offset_ms` offset: #90.
- Measuring sync and latency on real routes with the disk capture, and the decision on native sinks: #93.
- The capability-changed catalogue code: #332.
- Resolving `audio.output.device` from the settings into the sink's options: #54.
