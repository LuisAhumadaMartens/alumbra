# Rendition filtering

`umbra::abr` applies rendition filtering before the hybrid ABR rules choose a
candidate. `QueryRenditionCapabilities` translates manifest video tracks into
`CapabilityRequest` values and asks Penumbra's `CapabilityProvider` whether each
track is supported and smooth. It passes the video codec string, dimensions,
frame rate, bitrate, MIME type and HDR range. Query errors propagate to the
caller.

## Filter inputs and output

`FilterRenditions` is pure and deterministic. It receives generic rendition
descriptors, capability results and a value policy. Its eligible indices
identify the only descriptors the ABR policy may consider. Its excluded entries
carry the descriptor identifier and one reason: unsupported, not smooth, manual
selection, maximum height, maximum bitrate, data saver, CMSD, decode-drop cap or
viewport. If all descriptors are excluded, the policy returns those reasons
alongside its `kNoCandidates` decision.

The policy converts its candidates to descriptors and passes the filter result
to its selection rules. CMSD and decode-drop limits are part of the filter
policy, so their exclusions are returned with the other reasons.
`AbrDecision::excluded_renditions` preserves the filter's exclusions for the
caller. The filter and policy do not read platform or playback state while
deciding.

The limits are intersected:

- `abr.max_height` and `abr.max_bitrate_bps` use zero for no limit;
- data-saver and CMSD bitrate caps and a decode-drop height cap can be supplied
  by the policy input;
- when `abr.cap_to_viewport` is true, the cap is the smallest supported and
  smooth rendition height at least as large as the viewport in device pixels;
- if all available renditions are shorter than the viewport, the tallest
  supported and smooth rendition is the viewport cap;
- a manual selection keeps only the selected track after capability checks.

## Settings timing

The schema marks `abr.max_height` and `abr.max_bitrate_bps` as `next_load`.
The caller takes one settings snapshot when a load starts and uses that snapshot
for every ABR decision during that load. New values apply to the next load.
`abr.cap_to_viewport` is `live`; the caller supplies its current value and the
current viewport height on each decision. Resizing therefore recalculates the
eligible indices without replacing the load snapshot. The filter itself only
receives values and does not access the settings store.

The presentation model supplies frame rate, bit depth and video range. The
capability query includes frame rate and range; codec profile and bit depth
travel in the codec string as defined by ADR-0029.
