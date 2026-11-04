# Web output selection

The web selection policy follows [ADR-0036](../adr/0036-web-output-selection-and-ffmpeg-wasm-addon.md). The
portable policy and browser probes are in `web/output_selection.mjs`; browser APIs are injected through
`CapabilitySession`, so the decision table can run in Node without a browser.

## Probe inputs

Each presentation supplies renditions containing audio and video stream descriptions. A stream has a stable `id`,
`type`, RFC 6381 `codec`, coded dimensions where applicable, a `frameRate`, bitrate, and a browser `mimeType`. When
Alumbra can remux a container gap to fMP4, `mseMimeType` is the resulting fMP4 type. The caller supplies that value
only when the own-demuxer remux path is available.

`CapabilitySession` probes `MediaSource.isTypeSupported`, or
`ManagedMediaSource.isTypeSupported` when MediaSource is missing. iPhone callers select Managed Media Source
explicitly. It calls `MediaCapabilities.decodingInfo` for media-source and file configurations with the stream's
actual frame rate, then checks the matching `VideoDecoder.isConfigSupported` or
`AudioDecoder.isConfigSupported`. A session caches each distinct stream and probe mode, including pending probe
promises. Missing APIs and rejected probes count as unsupported.

## Selection and filtering

`PresentationOutputSelection` makes the choice once for its presentation. A new instance is created for each
presentation; a recovery that needs a different path restarts the presentation. `SelectOutputPath` applies the hard
rules before preferences: encrypted media and HDR require MSE, and iPhone
playback uses Managed Media Source. An active IMA provider requires MSE unless `engine.output_path` explicitly
selects WebCodecs. With `auto`, MSE wins when it can play at least one complete rendition; WebCodecs is considered
when no rendition is playable through MSE. All audio and video streams in a rendition must be supported by the
chosen path. The result marks eligible renditions so the caller can filter them before ABR.

The policy keeps audio and video on one output path. A missing browser decoder may use the FFmpeg add-on only when
`decode.addon_enabled` is true, an add-on URL is configured, and the video height is within
`decode.software_max_height` (zero means no configured cap). That layer still uses the WebCodecs-style output and
does not mix WASM audio with MSE video. A later add-on loader must confirm its codec support and integrity before
playback; this policy reports that software decoding is required.

An unavailable explicit path preference falls back to the other browser path with a warning code. The result
contains the selected path, reason code, device class, per-stream probe results, eligible renditions, warning codes
and add-on requirements. The optional log callback receives the selected path, reason, probes, eligible renditions
and warnings. This result is the input for the future web stats surface and device compatibility record.

## Tests and integration

Run the deterministic policy and probe tests with:

```sh
node --test web/output_selection.test.mjs
```

The tests cover the selection branches, explicit preference fallback, real frame-rate probing, per-session cache,
mixed-codec rendition filtering and the add-on height limit. The web port and Playwright conformance harness are not
present on this branch. The future port must pass its actual browser APIs into `CapabilitySession`, provide
presentation stream descriptions, apply the eligible rendition set before ABR, and publish probe and decision
results to stats and logs.
