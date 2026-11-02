# Rendition filtering

`umbra::abr` provides a reusable rendition filter. The first ABR stage required
by [ADR-0033](../adr/0033-abr-algorithm-and-bandwidth-estimation.md) uses it to
remove tracks that the decoder cannot use or that exceed a viewport or configured
cap. Ad media-file selection can use the same filter with its own descriptors.
The later ABR rules consume eligible indices. The filter does not select a
rendition or change playback state.

## Capability queries

`QueryRenditionCapabilities` translates manifest video tracks into
`CapabilityRequest` values and calls Penumbra's `CapabilityProvider::QueryDecoding`.
The request carries the RFC 6381 video codec string, dimensions, frame rate, bitrate,
MIME type and HDR range. A track is eligible only when the provider reports both
`supported` and `smooth`. Query errors propagate to the caller; unsupported
results are recorded as filtering reasons.

## Pure filter

`FilterRenditions` receives generic rendition descriptors, their capability
results and a value policy. Each eligible entry is the index of its descriptor;
each excluded entry carries its identifier and reason. It has no settings,
platform or playback state of its own. Callers can repeat it after a viewport
resize or a settings change, and the same inputs always produce the same eligible
indices and exclusion reasons.

The caps are intersected:

- `abr.max_height` and `abr.max_bitrate_bps` use zero for no limit;
- a data-saver bitrate cap can be supplied by the ABR policy;
- when `abr.cap_to_viewport` is true, the cap is the smallest supported and
  smooth rendition height at least as large as the viewport in device pixels;
- if all available renditions are shorter than the viewport, the tallest one is
  the viewport cap;
- a manual selection keeps only the selected track after all capability checks.

An exclusion has one reason, in precedence order: unsupported, not smooth,
manual selection, maximum height, maximum bitrate, data saver and viewport. An empty eligible
list and its reasons are returned to the caller so the playback source can report
the source-not-supported error with useful detail.

The three cap settings are live settings. The ABR owner reads them on each filter
pass, so settings changes and viewport changes can alter the eligible list without
recreating the port player. The presentation model supplies frame rate, bit depth
and video range; the capability query includes frame rate and range, while codec
profile and bit depth travel in the codec string as defined by ADR-0029.

## Tests

`umbra_abr_unittests` covers codec capability filtering, viewport resizing,
combined bitrate, height and manual caps, request construction, settings and
invalid cap values. Playback integration belongs to the ABR rule engine and the
headless CLI work item.
