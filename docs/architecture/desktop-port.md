# Desktop port assembly

`penumbra::desktop::CreatePort` assembles the desktop implementations behind
the current Penumbra `Port` contract. Its options are resolved values supplied by
the embedder; it does not read the settings registry itself. The embedder maps
`audio.output.device`, decoder limits and options, HTTP connection and redirect
settings, and the buffer byte budgets into those values.

The port provides the desktop capability query, FFmpeg and dav1d decoder
factory, SDL audio and video sinks, curl transport, file and HTTP(S) byte-stream
providers, task runners, a stderr log sink and a memory-pressure observer slot.
Headless audio selects SDL's dummy driver. Headless video uses a null sink.
Windowed mode creates an SDL window when the first video sink is requested.

`DesktopPortOptions` carries the resolved decoder settings, decoder capability
limits, display rate-match choice, audio device, HTTP timeouts and redirect
policy, user agent, buffer budgets and optional CA bundle. The source pipeline
owns the HTTP idle-read timeout; the HTTP transport enforces connection timeout
and redirect policy. Decoder fallback policy takes effect in the reference
player. Display rate matching remains an integration point until the player
receives presenter timing.

The reference-player target remains an in-tree, uninstalled implementation
library. Its internal Penumbra dependencies include Abseil and non-installed
contract headers, so it does not meet the installed C++20 header boundary.
`CreatePlayer` composes the decoder chain, media clock, audio conversion, and
output sinks. Decoder requests are held while playback rate is zero and
delivered after a positive rate is set. Audio conversion runs on the control
sequence and feeds a bounded PCM ring read by the desktop audio callback. The
conformance kit is registered against the headless desktop port, with
test-hook-only and unsupported output-mode cases skipped. The current Umbra
MP4/TS demuxers are also prerequisites for the conformance-clip integration
path.

The catalogue exposes file, HTTP and HTTPS byte streams. `Player::Create`
registers them with the source scheme registry. Progressive file and HTTP
resources pass through source detection and the Umbra demuxer registry; HLS and
DASH manifests use the shared curl transport for segment requests. HTTP byte
streams carry range requests through that transport and inherit its
cancellation and pause controls.
