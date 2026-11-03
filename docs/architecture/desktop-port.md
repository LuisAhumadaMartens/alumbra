# Desktop port assembly

`penumbra::desktop::CreatePort` assembles the desktop implementations behind
the Penumbra v3 `Port` contract. Its options are resolved values supplied by
the embedder; it does not read the settings registry itself. The embedder maps
`audio.output.device`, decoder limits and options, HTTP connection and redirect
settings, and the buffer byte budgets into those values.

The port provides the desktop capability query, FFmpeg and dav1d decoder
factory, SDL audio and video sinks, curl transport, file and HTTP(S) byte-stream
providers, task runners, a stderr log sink and a memory-pressure observer slot.
Headless audio selects SDL's dummy driver. Headless video uses the presenter's
off-screen target. SDL video initialisation is delayed until a video sink is
requested, so creating a headless port does not require a display. Windowed
mode creates an SDL window when the first video sink is requested.

`DesktopPortOptions` carries the resolved decoder settings, decoder capability
limits, display rate-match choice, audio device, HTTP timeouts and redirect
policy, user agent, buffer budgets and optional CA bundle. The source pipeline
owns the HTTP idle-read timeout; the HTTP transport enforces connection timeout
and redirect policy. Display rate matching and decoder fallback policy take
effect when the reference player is connected.

The reference-player target remains an in-tree, uninstalled implementation
library. Its internal Penumbra dependencies include Abseil and non-installed
contract headers, so it does not meet the installed C++20 header boundary.
`CreatePlayer` remains unavailable until the reference player implementation
and its media clock are complete. The port assembly therefore does not yet
provide end-to-end playback or pass the player-dependent suites in the
conformance kit. The current Umbra MP4/TS demuxers are also prerequisites for
the conformance-clip integration path.

The catalogue exposes file, HTTP and HTTPS byte streams. Elementary-stream
providers are empty until the desktop demuxers are connected to the source
pipeline. HTTP byte streams carry range requests through the shared curl
transport and inherit its cancellation and pause controls.
