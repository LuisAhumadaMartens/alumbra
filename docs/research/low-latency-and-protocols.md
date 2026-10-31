# Low latency and protocols

Last verified: 2026-10-31

This note covers the two HTTP low-latency profiles Alumbra must play (LL-HLS and LL-DASH) and the typical latency
each achieves. It also surveys the non-HTTP protocols in ADR 0011's source list (SRT, RTMP, RTSP, UDP/multicast) and
WebRTC with WHIP/WHEP, separating what a browser can do from what needs a native port. It closes with library and
licensing notes that feed D-13 (live model) and D-14 (protocol implementation and URL resolution).

## 1. Latency ranges

| Delivery | Latency figure | Source |
|----------|----------------|--------|
| Classic HLS, 6 s segments | Start at least 18 s behind the playlist end. `HOLD-BACK` defaults to 3 × target duration; encode, package and CDN delay come on top | Derived from [draft-pantos-hls-rfc8216bis-22 §4.4.3.8](https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22) |
| Broadcast TV | "Most measurements converge on a 3 - 10 seconds latency"; channel change "1-2 seconds" | [DASH-IF CR Low-Latency Modes](https://dash-industry-forum.github.io/docs/CR-Low-Latency-Live-r8.pdf) |
| LL-DASH service offering | "Target latency of typically 2 to 10 seconds" | DASH-IF CR |
| LL-HLS | Reduces latency "into the range of standard television broadcasts" (no number given) | [Apple, Enabling LL-HLS](https://developer.apple.com/documentation/http-live-streaming/enabling-low-latency-http-live-streaming-hls) |
| SRT link | Receiver buffer `SRTO_RCVLATENCY`, default 120 ms in live mode (a transport buffer, not end-to-end latency) | [libsrt socket options](https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md) |
| WebRTC | Sub-second end to end (unverified, commonly cited; depends on jitter buffer and network) | — |

## 2. LL-HLS

LL-HLS is part of the HLS second-edition draft
([draft-pantos-hls-rfc8216bis-22](https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22), 2026-05-01).
Apple's guide says the specification defines the low-latency extensions "in revision 7 and later". Its example uses
6 s segments with 200 ms partial segments, and its sample playlist uses "a version number of six" because the
low-latency syntax is backward-compatible.

| Feature | Tag or directive (draft section) | Purpose |
|---------|-----------------------------------|---------|
| Partial segments | `EXT-X-PART-INF:PART-TARGET` (§4.4.3.7); `EXT-X-PART` with `URI`, `DURATION`, `INDEPENDENT`, `BYTERANGE`, `GAP` (§4.4.4.9) | Publish sub-segment chunks before the parent segment completes |
| Preload hints | `EXT-X-PRELOAD-HINT:TYPE=PART\|MAP` (§4.4.5.3) | Let the client request the next part before it is listed |
| Server control | `EXT-X-SERVER-CONTROL` (§4.4.3.8): `CAN-BLOCK-RELOAD`, `CAN-SKIP-UNTIL`, `CAN-SKIP-DATERANGES`, `HOLD-BACK`, `PART-HOLD-BACK` | Advertise capabilities and recommended start distance |
| Blocking playlist reload | `_HLS_msn=M` and optional `_HLS_part=N` (§6.2.5.2) | The server holds the request until segment M (part N) exists, replacing polling |
| Delta updates | `_HLS_skip=YES` or `v2` (§6.2.5.1); `EXT-X-SKIP` (§4.4.5.2) | Send only the tail of long live playlists; `v2` also skips old date ranges |
| Rendition reports | `EXT-X-RENDITION-REPORT:URI,LAST-MSN,LAST-PART` (§4.4.5.4) | Let the client switch renditions without an extra round trip |

Normative details worth encoding in Umbra's live loader:

- **Hold-back values.** `HOLD-BACK` must be at least 3 × target duration and defaults to that value. `PART-HOLD-BACK`
  must be at least 2 × the part target and "SHOULD be at least three times" it. `CAN-SKIP-UNTIL` must be at least
  6 × target duration.
- **Blocking reload errors.**
  - If `_HLS_msn` is more than two segments beyond the last segment, or `_HLS_part` exceeds the last part by the
    Advance Part Limit (3 ÷ part target if the part target is under 1 s, otherwise 3), the server SHOULD return
    HTTP 400.
  - `_HLS_part` without `_HLS_msn` MUST also be answered with 400.
  - After blocking for more than three target durations, the server SHOULD return 503.
- **Server profile.** Appendix B.1 defines the LL-HLS server profile: `EXT-X-PROGRAM-DATE-TIME`, HTTP/2 or HTTP/3, and
  RFC 9218 priorities. Clients SHOULD verify the profile before playing less than two target durations from live,
  and SHOULD fall back to regular latency otherwise.

Client support:

- **Apple.** AVFoundation implements LL-HLS natively (Apple guide).
- **hls.js.** [hls.js](https://github.com/video-dev/hls.js/blob/master/docs/API.md) `lowLatencyMode` (default `true`)
  enables "part playlist and segment loading" and starts at `PART-HOLD-BACK`. `liveSyncMode` and
  `maxLiveSyncPlaybackRate` control catch-up.
- **Media3.** The [Media3 HLS table](https://developer.android.com/media/media3/exoplayer/hls) marks LL-HLS (Apple) as
  supported and the older "community" LL-HLS as unsupported. Delta updates, blocking reload and preload hints are
  supported, except byte ranges of undefined length.

## 3. LL-DASH

LL-DASH combines CMAF chunked encoding with HTTP chunked transfer, plus MPD signalling for latency targets:

- **Chunked segments.** Each segment holds several `moof`/`mdat` chunks. The MPD sets `@availabilityTimeComplete="false"`
  and an `@availabilityTimeOffset`, so the client may request a segment before it is complete and receive chunks as
  they are produced over HTTP/1.1 chunked transfer encoding. dash.js requires the Fetch API for this
  ([dash.js low-latency guide](https://github.com/Dash-Industry-Forum/dash.js/blob/development/docs/site/pages/usage/low-latency.md)).
- **Service description.** `<ServiceDescription>` carries `<Latency target min max referenceId>` in milliseconds and
  `<PlaybackRate min max>`. The DASH-IF CR defines target, minimum and maximum latency relative to a producer reference
  time, with an example of `<Latency target="3500" min="2000" max="10000">`.
- **Catch-up.** The player adjusts the playback rate within the signalled bounds, or seeks if it drifts too far.
  dash.js exposes `streaming.delay.liveDelay`, `streaming.liveCatchup.maxDrift` and
  `streaming.liveCatchup.playbackRate`.
- **ABR.** Throughput estimation is hard because a chunked download lasts about as long as the segment. dash.js ships
  the low-latency ABR rules L2A and LoL+.
- **Specifications.**
  - The [DASH-IF IOP v5 page](https://dashif.org/guidelines/iop-v5/) points Part 4 (low latency) to the CR.
  - [L3D](https://dashif.org/DASH-IF-IOP/l3d/) is the newer living document, maintained by the SVTA DASH-IF working
    group.
- **Clients.** dash.js has supported CMAF low latency since 2.6.8. Media3's
  [DASH table](https://developer.android.com/media/media3/exoplayer/dash) marks "Ultra low-latency CMAF live playback"
  as supported.

## 4. Protocols: browser vs native

Web pages have no raw TCP or UDP sockets:

- [WebTransport](https://developer.mozilla.org/en-US/docs/Web/API/WebTransport_API) (Chrome 97, Firefox 114,
  Safari 26.4 per MDN data) speaks HTTP/3 to a cooperating server, not arbitrary protocols.
- The WICG [Direct Sockets](https://wicg.github.io/direct-sockets/) API is exposed only in `[IsolatedContext]`, that
  is, Isolated Web Apps, not ordinary sites.

Every non-HTTP protocol below is therefore native-only, unless a server gateway converts it to HLS, DASH or WebRTC.

| Protocol | Specification status | Transport | Browser | Native implementation options |
|----------|----------------------|-----------|---------|-------------------------------|
| HLS / DASH over HTTP(S) | HLS draft rev 22; ISO/IEC 23009-1 | TCP (HTTP/1.1, HTTP/2), QUIC (HTTP/3) | Yes (MSE, or native HLS in Safari) | Umbra's own loaders |
| SRT (Secure Reliable Transport) | [draft-sharabayko-srt](https://datatracker.ietf.org/doc/draft-sharabayko-srt/) (Independent Submission, expired) | UDP, with ARQ and encryption | No | [libsrt](https://github.com/Haivision/srt) (latest v1.5.7); FFmpeg `srt://` via libsrt |
| RIST | (spec status not verified here) | UDP | No | [librist](https://code.videolan.org/rist/librist); FFmpeg `rist://` via librist |
| RTMP / Enhanced RTMP | Adobe RTMP 1.0 ([mirror](https://github.com/veovera/enhanced-rtmp/blob/main/docs/legacy/rtmp-v1-0-spec.pdf)); [E-RTMP v2](https://github.com/veovera/enhanced-rtmp) | TCP | No | FFmpeg native `rtmp`, `rtmps`, `rtmpt`, `rtmpe`; `librtmp`; Media3 RTMP data source (LibRtmp Client for Android) |
| RTSP + RTP | RTSP 1.0 [RFC 2326](https://www.rfc-editor.org/rfc/rfc2326), RTSP 2.0 [RFC 7826](https://www.rfc-editor.org/rfc/rfc7826); RTP [RFC 3550](https://www.rfc-editor.org/rfc/rfc3550) | TCP control; RTP over UDP or interleaved TCP | No | FFmpeg `rtsp` demuxer; [Media3 RTSP](https://developer.android.com/media/media3/exoplayer/rtsp) (UDP unicast or TCP interleaved; multicast unsupported) |
| UDP / multicast (usually MPEG-TS) | — | UDP | No | FFmpeg `udp://` (multicast join, source filters, threaded circular buffer) |
| WebRTC playback via WHEP | WHIP = [RFC 9725](https://www.rfc-editor.org/rfc/rfc9725) (Standards Track, 2025-03); WHEP = [draft-ietf-wish-whep-05](https://datatracker.ietf.org/doc/draft-ietf-wish-whep/) (submitted to the IESG) | UDP (ICE, DTLS-SRTP) | Yes ([RTCPeerConnection](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API)); WHEP is plain HTTP + SDP | A native WebRTC stack (choice and license unverified) |

Protocol notes:

- **Enhanced RTMP.** E-RTMP v2 (document version v2-2026-01-31-r2) adds FourCC signalling for VP8, VP9, HEVC, VVC and
  AV1, audio codecs AC-3, E-AC-3, Opus and FLAC, and multitrack. Listed contributors include Adobe, Google, Twitch and
  Jean-Baptiste Kempf (FFmpeg, VideoLAN).
- **FFmpeg RTSP.** FFmpeg documents `rtsp` as a demuxer and muxer, not a URL protocol, over RTP or RDT transport
  ([ffmpeg-protocols](https://ffmpeg.org/ffmpeg-protocols.html)).
- **WHIP and WHEP.** WHIP is for ingest; WHEP is its playback counterpart and the relevant one for a player.

## 5. "SRT": protocol vs subtitle format

Two unrelated technologies share the acronym:

| | SRT, the protocol | SRT, the subtitle format |
|-|-------------------|--------------------------|
| Full name | Secure Reliable Transport (Haivision, open source) | SubRip Text |
| What it is | UDP transport for live contribution and distribution | "A text-based file format" with the `.srt` extension ([Library of Congress FDD fdd000569](https://www.loc.gov/preservation/digital/formats/fdd/fdd000569.shtml)) |
| Appears as | URL scheme `srt://host:port?...` | File extension or text track |
| Alumbra layer | Umbra source and network (native only) | Umbra subtitles (all platforms) |

Disambiguation rule for Umbra:

- **Scheme decides.** The `srt://` URL scheme always selects the transport.
- **Extension or content type decides for files.** A `.srt` file fetched over `http(s)`/`file` is SubRip and is only
  valid as a text track. It is never a primary media source.
- **Naming.** Docs and APIs should spell out "SRT (Secure Reliable Transport)" or "SubRip" on first use.

## 6. Libraries and licensing

| Library | License | Notes |
|---------|---------|-------|
| FFmpeg (libavformat protocols) | LGPL 2.1 or later; GPL if optional GPL parts are enabled | [Legal page](https://ffmpeg.org/legal.html): FFmpeg is "not available under any other licensing terms" |
| libsrt | MPL-2.0 ([LICENSE](https://github.com/Haivision/srt/blob/master/LICENSE)) | Weak, file-level copyleft. Not listed in FFmpeg's LICENSE.md compatibility lists |
| librist | BSD-2-Clause (COPYING, VideoLAN and librist authors) | Used by FFmpeg `rist://` |
| librtmp (rtmpdump) | LGPL; the `rtmpdump` tool itself is GPLv2 ([rtmpdump](https://rtmpdump.mplayerhq.hu/)) | FFmpeg's native RTMP implementation avoids this dependency |
| LibRtmp Client for Android | Repository LICENSE is Apache-2.0, but it bundles librtmp sources under LGPL 2.1 ([repo](https://github.com/ant-media/LibRtmp-Client-for-Android)) | The [Media3 RTMP README](https://github.com/androidx/media/blob/release/libraries/datasource_rtmp/README.md) says it "is licensed separately" from Media3's Apache 2.0 |

FFmpeg licensing rules that affect Alumbra's builds:

- **LGPL checklist** ([legal page](https://ffmpeg.org/legal.html)):
  - build without `--enable-gpl` and `--enable-nonfree`;
  - link dynamically;
  - distribute FFmpeg's source with the binaries.
- **External libraries** ([LICENSE.md](https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md)):
  - x264, x265 and other GPL v2 libraries require `--enable-gpl`.
  - Apache-2.0 libraries (mbedTLS among them) require `--enable-version3`.
  - FDK AAC and OpenSSL require `--enable-nonfree`, which makes the binary "unredistributable".
- **Alumbra's position.** Alumbra is Apache-2.0 (repository `LICENSE`). Any FFmpeg protocol use must stay within an
  LGPL, dynamically linked configuration. Desktop TLS for `rtmps` needs a non-OpenSSL backend or a separate review.

## 7. Implications for Alumbra

**D-13: live model**

- **One live-edge model** for HLS and DASH, with these parameters:
  - target latency, from `PART-HOLD-BACK`/`HOLD-BACK` or `ServiceDescription`;
  - minimum and maximum latency;
  - playback-rate bounds for catch-up.
- **Playback-rate catch-up.** Requires Penumbra audio sinks that support small rate changes, ideally with pitch
  preservation. This is a new contract requirement compared with plain `SbPlayer` playback.
- **Low-latency loaders.**
  - LL-HLS needs blocking reload, preload hints and HTTP/2.
  - LL-DASH needs chunked fetch, with ABR estimators that tolerate chunked downloads (L2A or LoL+ style).
- **Time model.** Wall-clock anchoring through `EXT-X-PROGRAM-DATE-TIME` or `availabilityStartTime` and a producer
  reference time. This needs clock synchronization (for example a DASH `UTCTiming` source).

**D-14: protocol implementation and URL resolution**

- **FFmpeg protocols.**
  - Pro: one dependency covers `rtmp*`, `rtsp`, `udp` (multicast), `srt` and `rist`.
  - Cons: LGPL build discipline, coarse control over SRT statistics and latency settings, and FFmpeg's I/O model.
- **Dedicated libraries.**
  - Pro: libsrt and librist offer full socket options and statistics.
  - Con: more integration work.
- **Possible hybrid (to evaluate).** FFmpeg for RTSP, RTMP and UDP demux; libsrt directly for SRT.
- **URL resolution.** Umbra keeps a scheme registry:
  - `http`, `https` and `file` are available everywhere.
  - `rtmp`, `rtsp`, `srt` and `udp` exist only when the active Penumbra port registers a protocol provider.
  - On the web, unsupported schemes fail fast with a clear `MediaError`; a server-side gateway is out of scope.
  - WHEP could later become a web-capable source type, because WebRTC is available in browsers.

## References

- R. Pantos. *HTTP Live Streaming 2nd Edition, draft-pantos-hls-rfc8216bis-22*. https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis-22
- Apple. *Enabling Low-Latency HTTP Live Streaming (HLS)*. https://developer.apple.com/documentation/http-live-streaming/enabling-low-latency-http-live-streaming-hls
- hls.js. *API.md*. https://github.com/video-dev/hls.js/blob/master/docs/API.md
- Android Developers. *Media3 HLS*. https://developer.android.com/media/media3/exoplayer/hls
- Android Developers. *Media3 DASH*. https://developer.android.com/media/media3/exoplayer/dash
- Android Developers. *Media3 RTSP*. https://developer.android.com/media/media3/exoplayer/rtsp
- DASH-IF. *dash.js low-latency streaming*. https://github.com/Dash-Industry-Forum/dash.js/blob/development/docs/site/pages/usage/low-latency.md
- DASH-IF. *Change Request: Low-latency Modes for DASH (r8)*. https://dash-industry-forum.github.io/docs/CR-Low-Latency-Live-r8.pdf
- DASH-IF. *IOP v5 guidelines*. https://dashif.org/guidelines/iop-v5/
- DASH-IF / SVTA. *L3D*. https://dashif.org/DASH-IF-IOP/l3d/
- Haivision. *SRT repository*. https://github.com/Haivision/srt
- Haivision. *SRT LICENSE (MPL-2.0)*. https://github.com/Haivision/srt/blob/master/LICENSE
- Haivision. *SRT v1.5.7 release*. https://github.com/Haivision/srt/releases/tag/v1.5.7
- Haivision. *SRT socket options*. https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md
- IETF Datatracker. *draft-sharabayko-srt*. https://datatracker.ietf.org/doc/draft-sharabayko-srt/
- Library of Congress. *SubRip Subtitle format (fdd000569)*. https://www.loc.gov/preservation/digital/formats/fdd/fdd000569.shtml
- VideoLAN. *librist*. https://code.videolan.org/rist/librist
- Veovera. *Enhanced RTMP*. https://github.com/veovera/enhanced-rtmp
- Adobe. *RTMP 1.0 specification (mirror)*. https://github.com/veovera/enhanced-rtmp/blob/main/docs/legacy/rtmp-v1-0-spec.pdf
- RTMPDump. *rtmpdump and librtmp*. https://rtmpdump.mplayerhq.hu/
- Ant Media. *LibRtmp Client for Android*. https://github.com/ant-media/LibRtmp-Client-for-Android
- AndroidX Media. *RTMP DataSource module README*. https://github.com/androidx/media/blob/release/libraries/datasource_rtmp/README.md
- IETF. *RFC 2326: RTSP 1.0*. https://www.rfc-editor.org/rfc/rfc2326
- IETF. *RFC 7826: RTSP 2.0*. https://www.rfc-editor.org/rfc/rfc7826
- IETF. *RFC 3550: RTP*. https://www.rfc-editor.org/rfc/rfc3550
- IETF. *RFC 9725: WHIP*. https://www.rfc-editor.org/rfc/rfc9725
- IETF Datatracker. *draft-ietf-wish-whep*. https://datatracker.ietf.org/doc/draft-ietf-wish-whep/
- MDN. *WebTransport API*. https://developer.mozilla.org/en-US/docs/Web/API/WebTransport_API
- MDN. *WebRTC API*. https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API
- WICG. *Direct Sockets API*. https://wicg.github.io/direct-sockets/
- FFmpeg. *Protocols documentation*. https://ffmpeg.org/ffmpeg-protocols.html
- FFmpeg. *License and legal considerations*. https://ffmpeg.org/legal.html
- FFmpeg. *LICENSE.md*. https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md
