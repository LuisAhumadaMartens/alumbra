# VAST parser and wrapper resolution

`umbra::ads` parses a VAST response into its own value types. The types retain
the ad id, sequence and ad type; inline media files and mezzanine files;
universal ad ids; verification resources; tracking and click URLs; and raw XML
for extension elements. The `sequence` value remains on each ad so the AdManager
can form pods without making this parser depend on the ad-break model.

The parser accepts VAST 4.x and compatible 2.0 and 3.0 responses. It does not
load a schema or another XML library. It rejects DTD declarations, unsupported
entity references, malformed XML, input over the configured byte limit,
elements deeper than the configured nesting limit and documents over the
configured element count. XML external resources are never resolved.

`ResolveVastWrappers` accepts a fetch callback. The callback receives the
wrapper URI and its timeout budget; the resolver applies the response-size and
depth limits, detects repeated wrapper URIs, and merges impression, error,
tracking, click, verification and extension data into the inline ad. A failed
fetch maps to VAST error 301, a depth overrun to 302 and a response with no ads
to 303. The caller supplies the fetch callback so the provider integration can
use the platform HTTP stack and the AdManager lifecycle.

The settings schema defines the wrapper depth, request timeout and response
size. Their defaults are five wrappers, 5000 ms and 1 MiB. The parser and
resolver accept explicit options so tests can exercise limits without changing
global settings.


## Linear media selection

The built-in selector skips mezzanine files, VPAID files, files with an unsupported delivery type, and MIME types that are not video or a supported streaming playlist. It queries the Penumbra capability provider for each remaining file and keeps only files marked supported and smooth. The ad media bitrate cap and estimated bandwidth are hard limits. Among eligible files, selection prefers the configured delivery, then the dimensions nearest the player viewport, then the highest bitrate. If no file remains, selection returns VAST error 403.

The VAST `bitrate` attribute is expressed in kilobits per second. Parsed media files store it in bits per second so selection can compare it with the capability query and bandwidth estimate. `ads.media.max_bitrate_kbps` converts to bits per second when the setting snapshot is read.
