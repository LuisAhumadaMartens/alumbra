# 0027 — Resolve URLs through a scheme registry and build live protocols on libsrt and shared native code

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #14

## Context

[ADR-0011](0011-sources-open-network-stream.md) accepts any typed source URL, in the spirit of VLC's "Open Network
Stream", and fixes the schemes: `http(s)` everywhere, `file` on native platforms, and `rtmp`, `rtsp`, `srt` and `udp`
on native platforms only. D-14 decides how a URL becomes a transport plus a source type (progressive, HLS, DASH or raw
MPEG-TS), which schemes each port offers, and what implements the non-HTTP protocols. M1 needs only the resolver with
`http(s)` and `file` (#73), but the provider interface is shaped now, and the protocol choices gate M8 (#229–#233).

The signals that identify a source differ in reliability:

- an explicit type from the caller, like HTML's `<source type>`, is authoritative;
- the scheme is authoritative for the transport only;
- `Content-Type` is often wrong on misconfigured CDNs (`application/octet-stream`, `text/plain`);
- extensions are missing from signed or tokenised URLs;
- the first bytes identify most formats: `#EXTM3U`, an `MPD` root element, an `ftyp` box, the `0x47` sync byte every
  188 bytes, the EBML magic `1A 45 DF A3`. WHATWG MIME Sniffing does not cover HLS or DASH.

The platforms add constraints. Web pages cannot open raw sockets: Direct Sockets is exposed only to Isolated Web Apps.
On Apple platforms, App Transport Security (ATS) blocks cleartext HTTP by default, and
`NSAllowsArbitraryLoadsForMedia` covers only AV Foundation's requests, not Alumbra's own `URLSession` traffic. LAN
cameras trigger the local-network privacy prompt, and multicast needs an entitlement that Apple grants on request.
Android disables cleartext traffic by default from API 28 and needs a `WifiManager.MulticastLock` to receive
multicast. Names collide: `srt://` is Secure Reliable Transport, while `https://…/x.srt` is a SubRip subtitle file.
Units collide too: `srt-live-transmit` reads the `latency` query parameter in milliseconds, and FFmpeg's `srt`
protocol reads it in microseconds.

The issue recommends starting RTSP and RTMP on desktop through FFmpeg. Since it was written,
[ADR-0014](0014-dependency-and-licensing-policy.md) has closed that route, so the implementation question needs a new
answer.

Accepted decisions already settle several parts, and this ADR does not reopen them:

- **Schemes** (ADR-0011). An unsupported scheme fails fast with a clear `MediaError`.
- **Libraries** (ADR-0014). FFmpeg is built with `--disable-network --disable-protocols`, which is final, so it never
  opens files, URLs or sockets. LIVE555 is LGPL-3.0-or-later and denied. librtmp (LGPL-2.1-or-later) is eligible only
  on desktop and Android after review. libsrt (MPL-2.0) is allowed under conditions.
- **Redaction** ([ADR-0018](0018-error-model-and-recovery.md#redaction), refined by
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md#privacy-and-redaction)). URL user information and
  credential values never appear in diagnostics, in any mode. The issue's question about redaction rules is closed.
- **Per-source overrides** ([ADR-0017](0017-settings-schema-and-configuration.md#per-source-overrides)).
  `load(url, {overrides})` takes structured, validated overrides, and VLC-style URL options are left to D-14.
- **Threading** ([ADR-0015](0015-threading-and-task-runners.md)). Network callbacks arrive on the network sequence,
  nothing blocks a sequence, and work that may block carries the `MayBlock` trait.
- **Layering** ([layering checks](../architecture/layering.md)). Umbra includes no OS header and links no platform
  library, and ports never depend on each other. Socket code and libsrt therefore cannot live in Umbra, nor in one
  port that the others reuse.
- **Engine ownership** ([ADR-0016](0016-penumbra-port-contract.md)). Umbra owns networking policy, demuxing and ABR.
  The v1 port contract declares no network provider; D-08 (#8) adds the HTTP interface.

## Decision drivers

- The same protocols on every native platform, within ADR-0014's licence rules.
- Protocol code that runs on Alumbra's task runners, is fuzzed and shares one error model.
- The right source type despite wrong MIME types and tokenised URLs, at no extra round trip.
- An HTTP interface that stays HTTP-only, and providers shaped like their payloads.
- One unit per quantity in settings and in URLs, under ADR-0017's suffix rule.
- Credentials that never stay in URLs, logs or storage, while camera URLs keep working.

## Considered options

The definition has six questions.

Implementation of the non-HTTP protocols:

1. **A — libsrt plus own C++:** a scheme registry with port-registered providers. libsrt implements SRT, and own C++
   code shared by all native ports implements UDP/RTP carrying MPEG-TS, RTMP and RTSP.
2. **B — Own code, librtmp for RTMP:** as A, but RTMP uses librtmp on desktop and Android.
3. **C — Supersede ADR-0014 on desktop:** a new ADR re-enables FFmpeg's `rtsp` and `rtmp` on desktop only, as the
   issue proposes.

The issue's option B, LIVE555 for RTSP, is not considered, because ADR-0014 denies LGPL-3.0.

Shape of a protocol provider:

1. **A — Two provider kinds:** byte streams feed the demuxers, and RTSP emits elementary-stream samples.
2. **B — Everything as a byte stream:** RTSP is depacketised and then remuxed into MPEG-TS.
3. **C — Every provider demuxes:** every provider emits samples.

Order of signals and sniffing budget:

1. **A — The issue's order, 4 KiB:** explicit type, scheme, `Content-Type`, extension, then sniffing.
2. **B — Sniff first, reuse the bytes:** explicit type, scheme for the transport only, then the first bytes of the
   response already being read, with extension and `Content-Type` as tie-breakers.
3. **C — Large sniff:** always read 64 KiB before deciding.

Schemes per port:

1. **A — Shared C++ providers everywhere:** desktop, Android and Apple register every non-HTTP scheme from M8.
2. **B — Non-HTTP schemes on desktop only for 1.0.**
3. **C — Desktop and Android first, Apple later.**

SRT options, URL options and RIST:

1. **A — Canonical millisecond keys, libsrt URI conventions.**
2. **B — Also accept FFmpeg's conventions**, detecting microsecond values and converting them.
3. **C — No URL options**, only structured overrides.

Credentials:

1. **A — Callback, user information converted:** a credential callback, with `user:pass@` moved into a credential
   object.
2. **B — Callback only:** user information in URLs is rejected, as RFC 3986 deprecates it.
3. **C — User information only.**

## Decision

Chosen options: **A** for the implementation, the provider shape, the schemes per port, the options and the
credentials, and **B** for the resolution order. The first question's option A is the only implementation that
ADR-0014 allows on every native platform, and shared native providers make platform parity cheap. The first response
is read whatever the type, so sniffing it costs nothing and is the most reliable signal.

### Scheme registry

- Umbra keeps a registry that maps each scheme to a provider; schemes compare case-insensitively, as RFC 3986
  requires. A scheme is available only when the active port supplies what its provider needs: D-08's HTTP transport
  for `http` and `https`, file access for `file`, and the shared native providers below for the rest.
- A URL whose scheme has no provider fails at `load()`, before any I/O, with `MEDIA_ERR_SRC_NOT_SUPPORTED` and an
  ADR-0018 `ErrorDetail` that names the scheme and the platform. On the web that is every non-HTTP scheme.
- `srt://` always selects SRT. A `.srt` resource fetched over `http(s)` or `file` is SubRip: it is valid only as a
  text track (#134), and loading it as primary media fails with a message that points to text tracks.
- Umbra parses and resolves URLs as RFC 3986 specifies; HLS and DASH use the same rules for relative references.
- Two spellings extend ADR-0011's rows without adding a provider: `rtmps://` is RTMP over TLS, served by the RTMP
  provider, and `rtp://` names MPEG-TS in RTP, served by the UDP provider.

### Provider kinds

The Penumbra interface gains two provider interfaces. They are declared there because the shared native providers
implement them, and Penumbra code may not include Umbra headers. Both kinds are asynchronous and cancellable, deliver
on the network sequence, report failures as ADR-0018 statuses and make no callback after cancellation completes.
Socket and libsrt calls that may block run as `MayBlock` work, never on a sequence.

- **`ByteStream`** providers deliver container bytes to the normal source and demux path: `http` and `https`, `file`,
  `srt`, `udp`, and `rtmp`, which produces FLV. A byte stream reports whether it is seekable and whether its size is
  known; live transports are neither.
- **`ElementaryStream`** providers emit stream configurations and `EncodedSample`s (#51) directly, with no container:
  `rtsp`, whose RTP payloads are elementary streams. Their samples join the pipeline where demuxed samples do, so
  buffering, ABR and the port contract treat them like any others.
- The HTTP client that D-08 (#8) adds to the port contract is only the transport behind the `http` byte stream, which
  Umbra implements on top of it. The client stays HTTP-only. This answers D-08's question whether non-HTTP I/O sits
  behind the same interface: it does not.
- Changes to the port contract follow its compatibility rule, so a new pure virtual method increments
  `penumbra::kApiVersion`.

### Where the protocol code lives

- libsrt and Alumbra's own UDP, RTP, RTMP and RTSP code live in one shared native Penumbra component that the desktop,
  Android and Apple ports link. It is not a port. It depends on the Penumbra interface, the platform socket API,
  libsrt, and services the port injects, such as a TLS stream for `rtmps` and Android's multicast lock. The web port
  never links it.
- The layering allowlist lets ports share only the interface today. This ADR justifies adding the component to the
  allowlist's shared components, next to the interface; Umbra still may not reach it. #229 names the component and its
  target.
- Every parser of untrusted input in the component gets a registered libFuzzer target under
  [ADR-0021](0021-testing-strategy.md): RTMP chunks and AMF0 commands, RTSP responses, SDP, RTP and RTCP packets, and
  the depacketisers.

### Protocol implementations

- **`srt`: libsrt, called directly.** A byte stream, usually carrying MPEG-TS. It covers caller, listener and
  rendezvous modes, passphrase encryption and stream IDs, and libsrt's statistics feed ADR-0020's metrics (#230).
- **`udp` and `rtp`: own C++.** A byte stream carrying MPEG-TS, raw or in RTP (RFC 2250), unicast or multicast, with
  source-specific joins, RTP reordering and MPEG-TS continuity checks (#233).
- **`rtmp` and `rtmps`: own C++.** The handshake, the chunk stream and the AMF0 commands, for playback only. A byte
  stream producing FLV, including Enhanced RTMP codecs; `rtmps` runs over a TLS stream from the port (#231).
- **`rtsp`: own C++.** An elementary-stream provider. It speaks RTSP 1.0 (RFC 2326), and RTSP 2.0 (RFC 7826) where a
  server offers it, with SDP session descriptions. RTP and RTCP (RFC 3550) run over UDP unicast, interleaved TCP or
  multicast. Depacketisers cover H.264 (RFC 6184), H.265 (RFC 7798) and AAC (RFC 3640), and A/V sync comes from RTCP
  sender reports (#232).

The other routes are not used:

- FFmpeg's network protocols and its `rtsp` demuxer stay disabled, on desktop too (ADR-0014).
- LIVE555 stays denied.
- librtmp is not adopted. ADR-0014 would allow it only on desktop and Android, so Apple would have no RTMP. It is no
  longer actively maintained, its vcpkg port declares no licence, and it does not support Enhanced RTMP codecs.

### Staging

M1 builds the registry, both provider interfaces and the `http(s)` and `file` byte streams (#73, #74, #75). M8 adds
the native protocols in three steps: UDP (with RTP) and SRT first, then RTMP, then RTSP, whose camera
interoperability takes longest to mature.

### Source-type resolution

The resolver decides the source type once per load, in this order, and logs the deciding signal under ADR-0020's
redaction rules:

1. **Explicit type.** A type the caller states through the per-source override `source.type_hint` is authoritative.
   Umbra does not second-guess it: if the bytes do not parse as that type, the load fails.
2. **Scheme.** The scheme selects the transport only. `rtsp` needs no further decision, because its SDP describes each
   stream; `rtmp` delivers FLV; `srt` and `udp` normally carry MPEG-TS, which the sniff confirms.
3. **Sniffing.** Umbra inspects the first bytes of the response it is already reading. No extra request, such as a
   `HEAD` or a separate range request, is made to sniff, and the inspected bytes are handed to the chosen parser, so
   nothing is fetched twice.
4. **Extension and `Content-Type`.** They only break ties: they choose between types that the bytes leave open, and
   never override a decisive signature.

The sniffing budget is 4 KiB. It grows, up to 64 KiB, only while an XML or text prologue, such as a byte-order mark,
an XML declaration, comments or whitespace, hides the root element. If neither the signatures nor the tie-breakers
decide within the budget, the load fails with `MEDIA_ERR_SRC_NOT_SUPPORTED`. The signatures themselves come from
#76 and D-05 (#5).

The sniff decides the source type and, for progressive media, the container. #76's `source.probe.max_bytes` then
bounds how far the chosen demuxer reads to expose tracks and duration; it is not a second sniff. An MP4 with `moov` at
the end still makes its tail range request after the type is known, as #76 specifies.

### Schemes per port

- Every port registers `http`, `https` and `file` from its first milestone, and the web registers only `http` and
  `https`. In M1 that is the desktop port (#73).
- From M8, the desktop, Android and Apple ports register `srt`, `udp`, `rtmp` and `rtsp` through the shared
  providers.
- On Apple platforms, multicast works only when the app holds the `com.apple.developer.networking.multicast`
  entitlement, which Apple grants on request. Without it, multicast `udp` and RTSP sources fail with an error that
  says so, and unicast is unaffected. LAN sources also need `NSLocalNetworkUsageDescription` for the local-network
  privacy prompt.
- On Android, the port holds a `WifiManager.MulticastLock` only while a multicast source is playing.
- Alumbra never weakens ATS or Android's cleartext policy itself. Its packages ship no ATS exception, and no manifest
  entry or network security configuration that permits cleartext. A cleartext `http` load that a platform policy
  blocks fails with an error that names the policy, so it is not mistaken for a network fault.
- The integrator documentation describes the settings an app may choose: per-domain ATS exceptions, noting that
  `NSAllowsArbitraryLoadsForMedia` does not cover Alumbra's traffic, and Android's per-domain
  `cleartextTrafficPermitted`. ATS does not apply to lower-level networking interfaces, and Android does not expect
  the socket API to honour its cleartext flag, so neither policy governs the socket-based providers. The documentation
  also says which schemes travel in clear text.

### Protocol options

- Protocol settings live in the `network.` area with the scheme as the next segment, and times are in milliseconds
  under ADR-0017's suffix rule, for example `network.srt.latency_ms`. The keys that #229–#233 propose under `srt.`,
  `rtmp.`, `rtsp.`, `udp.` and `protocol.` move into this area when their tasks add them to the schema. They were
  never released, so no alias is needed.
- `srt://` query parameters follow libsrt's URI syntax as `srt-live-transmit` documents it, with times in milliseconds
  (`latency`, `rcvlatency`, `peerlatency`, `conntimeo`). The resolver normalises each one to its canonical key and
  validates it like any per-source override, so an invalid parameter fails before connecting.
- FFmpeg's microsecond spelling is not accepted, and no value is reinterpreted by its magnitude: `latency=120000`
  copied from ffplay means 120,000 milliseconds, never 120. The documentation shows the conversion.
- An SRT `passphrase` in a URL is a credential. Like user information, it is moved out of the URL into the credential
  object described below.
- VLC-style `:option=value` suffixes work only in `alumbra-cli`, which converts them to structured overrides before it
  calls `load()`. The engine and the SDKs never parse them.
- RIST (librist, BSD-2-Clause) is not part of M8. It is reconsidered after M8 as another shared provider.
- The provisional keys named here are `source.type_hint`, `network.srt.latency_ms` (default 120, libsrt's receive
  latency), `network.rtsp.transport` (`auto`, `udp` or `tcp`) and `network.udp.multicast_interface`. Their tasks add
  them to the schema with types and ranges.

### Credentials

- The preferred API is a credential callback that the integrator installs. Umbra asks it per origin and
  authentication realm when a server challenges, such as an HTTP or RTSP `401` with `WWW-Authenticate`, or when a
  protocol needs a secret before connecting, such as an SRT passphrase. The callback may answer asynchronously, so an
  app can prompt the user.
- On native platforms, `user:pass@` in a URL is accepted, because RTSP cameras rely on it. The resolver moves it at
  once into a credential object for that origin and strips it from the URL that it requests, keeps and reports, so
  logs, events and diagnostics only ever see the stripped URL.
- On the web, a URL with user information fails with an error that points to the callback. `fetch` refuses such URLs
  anyway.
- Credentials live in memory only, for as long as the source that uses them is loaded. Alumbra never persists them or
  shares them between players.
- ADR-0018 and ADR-0020 already keep credential values out of every diagnostic. SRT passphrases and RTSP Digest
  responses count as credential values.

### Corrections to the issue

The issue is corrected in five places to match accepted decisions:

1. RTSP and RTMP do not start on desktop through FFmpeg. ADR-0014 makes `--disable-network` final and forbids
   re-enabling any built-in protocol, and FFmpeg's `rtsp` demuxer opens its own sockets. Own shared code replaces that
   route on every native platform, desktop included.
2. LIVE555, the issue's option B, is LGPL-3.0-or-later, which ADR-0014 denies.
3. ADR-0014 limits librtmp to desktop and Android after review, never Apple or the web, so it cannot give RTMP parity.
4. libsrt's vcpkg licence expression is `MPL-2.0 AND BSD-3-Clause AND Zlib AND Unlicense`, not MPL-2.0 alone.
   `Unlicense` is not on ADR-0014's automatic list, so libsrt needs the recorded review below before it ships.
5. Socket code lives neither in Umbra, which the layering rules forbid, nor in a single port. It lives in shared
   native Penumbra code, and Umbra keeps the registry, the resolution and the policy.

### libsrt review

ADR-0014 requires a recorded review before libsrt enters a distributed build. #230 owns it, and #40 records it:

- **Version and features.** The pinned vcpkg baseline provides libsrt 1.5.7. The `bonding` and `tool` features stay
  off.
- **Licence expression.** The port installs four copyright files: `LICENSE` (MPL-2.0), `srtcore/udt.h`
  (BSD-3-Clause), `srtcore/md5.h` (Zlib) and `srtcore/atomic.h` (Unlicense). The review records whether the Unlicense
  terms are acceptable for every artefact and adds a reviewed exception to #40's check. Until then the check treats
  the expression as unresolved.
- **MPL conditions.** ADR-0014 requires covered files to stay unmodified, notices to be kept and a working pointer to
  the exact source. All three of the port's patches modify `CMakeLists.txt`, which is MPL-covered. The review
  therefore either publishes the patched file under MPL with the source pointer, or builds from an overlay port that
  leaves covered files unchanged.
- **Cryptography.** The port links OpenSSL (Apache-2.0) for SRT encryption, so OpenSSL ships wherever libsrt does and
  is recorded with it.

## Consequences

**Positive**
- Every native platform gets the same protocols, and MPL-2.0 is the only new licence condition, apart from the
  Unlicense review.
- Protocol code runs on Alumbra's task runners, shares one error model and is fuzzed.
- RTSP needs no artificial container, and the HTTP interface stays HTTP-only.
- Wrong `Content-Type` headers and tokenised URLs no longer break type detection, and sniffing costs no round trip.
- SRT latency has one unit everywhere.
- Camera URLs with user information work on native platforms, and credentials never stay in URLs or storage.
- Integrators keep control of their platforms' security policies.

**Negative / costs**
- Alumbra owns its RTMP, RTSP and UDP/RTP implementations, and RTSP camera interoperability takes time to learn.
- RTMP needs an own FLV parser, which is not in D-05's v1 own-parser set.
- Two provider kinds must be defined, tested and documented.
- The type decision waits for the first chunk of the response.
- SRT URLs copied from ffplay need their latency converted.
- Credentials have two input paths.
- Apple's multicast entitlement and local-network prompt complicate demos and tests on Apple platforms.
- The layering allowlist gains a shared component, and the port contract's version increments.

**Follow-ups**
- #73: the scheme registry, both provider interfaces, RFC 3986 parsing, the resolution order and sniffing budget,
  `source.type_hint`, the credential callback and credential object, user-information stripping and the SubRip rule.
- #74 and #75: the `file` and `http(s)` byte streams.
- #76: signature detection within the sniffing budget, keeping `source.probe.max_bytes` for initialisation.
- #229: the shared native component and its allowlist entry, live byte-stream semantics, the jitter buffer and clock
  recovery, `srt://` parameter normalisation and the `network.` keys. It also defines how a URL parameter and a
  structured override for the same key combine, and whether the Android providers honour `NetworkSecurityPolicy` per
  host. Its FFmpeg note is superseded.
- #230: SRT on libsrt after the review above. Its FFmpeg unit conversion is dropped.
- #231: RTMP and `rtmps` as own code producing FLV, instead of FFmpeg or librtmp.
- #232: RTSP as an elementary-stream provider instead of an FFmpeg adapter, with Basic and Digest authentication
  through the credential callback.
- #233: UDP and RTP MPEG-TS, the Apple multicast entitlement and the Android multicast lock.
- **libsrt licence review** (#230, #40): recorded as above before any build that ships libsrt.
- **D-08 (#8):** the HTTP transport behind the `http` byte stream, and the TLS stream that `rtmps` needs. A TLS
  library that D-08 does not already bring in needs its own review under ADR-0014.
- **D-05 (#5):** an own FLV parser, with Enhanced RTMP codecs and a fuzz target, joins the own-parser set in M8 for
  #231. ADR-0025's v1 set does not include it.
- The [compatibility matrix](../compatibility.md) and the integrator guides gain the schemes per platform, the ATS and
  cleartext settings, local-network privacy and multicast requirements, as #229 and #233 land.
- RIST is reconsidered after M8.

## Pros and cons of the options

### Implementation of the non-HTTP protocols

- **A. libsrt plus own C++.** Good, because MPL-2.0 is the only licence condition, and the code is portable to Android
  and Apple, fuzzable and runs on the task runners. Bad, because RTSP camera interoperability takes time, and RTMP
  needs a small own FLV parser.
- **B. Own code, librtmp for RTMP.** Good, because there is less RTMP code to write. Bad, because Apple gets no RTMP,
  librtmp is unmaintained and lacks licence metadata, and it has no Enhanced RTMP codecs.
- **C. Supersede ADR-0014 on desktop.** Good, because desktop interoperability arrives fastest. Bad, because it
  reopens an accepted licence and attack-surface decision, FFmpeg opens its own sockets outside the task runners, and
  mobile still needs another path.

### Shape of a protocol provider

- **A. Two provider kinds.** Good, because RTSP needs no artificial container and the HTTP interface stays HTTP-only.
  Bad, because there are two provider shapes to define.
- **B. Everything as a byte stream.** Good, because there is one shape. Bad, because the remux is wasteful and loses
  timestamp precision.
- **C. Every provider demuxes.** Good, because there is one shape. Bad, because demuxing is duplicated for
  byte-oriented protocols.

### Order of signals and sniffing budget

- **A. The issue's order, 4 KiB.** Good, because the type is known before the body is read. Bad, because it trusts
  `Content-Type`, which misconfigured CDNs get wrong.
- **B. Sniff first, reuse the bytes.** Good, because it is robust against wrong MIME types and tokenised URLs at no
  extra round trip. Bad, because the decision waits for the first chunk.
- **C. Large sniff.** Good, because it catches unusual files. Bad, because start-up is slower on slow links.

### Schemes per port

- **A. Shared C++ providers everywhere.** Good, because native platforms get VLC-like parity. Bad, because Apple's
  local-network prompt and multicast entitlement complicate demos.
- **B. Non-HTTP schemes on desktop only for 1.0.** Good, because M8 is smaller. Bad, because mobile has no "Open
  Network Stream".
- **C. Desktop and Android first, Apple later.** Good, because it avoids Apple's entitlement work. Bad, because the
  platforms become asymmetric.

### SRT options, URL options and RIST

- **A. Canonical millisecond keys, libsrt URI conventions.** Good, because units are consistent under ADR-0017's
  suffix rule, and libsrt's URI convention is the de facto standard. Bad, because ffplay-style SRT URLs need
  translating.
- **B. Also accept FFmpeg's conventions.** Good, because URLs paste straight from ffplay. Bad, because units become
  ambiguous.
- **C. No URL options.** Good, because it is simplest. Bad, because it breaks the "just paste a URL" promise for SRT.

### Credentials

- **A. Callback, user information converted.** Good, because it gives VLC-like convenience without leaks. Bad,
  because there are two input paths.
- **B. Callback only.** Good, because it is the cleanest model. Bad, because many RTSP camera URLs fail.
- **C. User information only.** Good, because it is trivial. Bad, because secrets live in URLs and in app storage.

## References

- [D-14 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/14)
- [Alumbra research: Low latency and protocols](../research/low-latency-and-protocols.md)
- libsrt 1.5.7: [API](https://github.com/Haivision/srt/blob/v1.5.7/docs/API/API.md) ·
  [socket options](https://github.com/Haivision/srt/blob/v1.5.7/docs/API/API-socket-options.md) ·
  [statistics](https://github.com/Haivision/srt/blob/v1.5.7/docs/API/statistics.md) ·
  [`srt-live-transmit` SRT URI syntax](https://github.com/Haivision/srt/blob/v1.5.7/docs/apps/srt-live-transmit.md#medium-srt) ·
  [licence](https://github.com/Haivision/srt/blob/v1.5.7/LICENSE)
- vcpkg at `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`: libsrt
  [manifest](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/libsrt/vcpkg.json)
  and [portfile](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/libsrt/portfile.cmake)
- [FFmpeg: `srt` protocol options](https://ffmpeg.org/ffmpeg-protocols.html#srt)
- [RFC 2326: RTSP 1.0](https://www.rfc-editor.org/rfc/rfc2326) ·
  [RFC 7826: RTSP 2.0](https://www.rfc-editor.org/rfc/rfc7826)
- [RFC 3550: RTP](https://www.rfc-editor.org/rfc/rfc3550) ·
  [RFC 2250: RTP payload for MPEG](https://www.rfc-editor.org/rfc/rfc2250) ·
  [RFC 6184: H.264](https://www.rfc-editor.org/rfc/rfc6184) ·
  [RFC 7798: HEVC](https://www.rfc-editor.org/rfc/rfc7798) ·
  [RFC 3640: MPEG-4 elementary streams](https://www.rfc-editor.org/rfc/rfc3640)
- [Adobe RTMP 1.0 specification (mirror)](https://github.com/veovera/enhanced-rtmp/blob/main/docs/legacy/rtmp-v1-0-spec.pdf) ·
  [Enhanced RTMP](https://github.com/veovera/enhanced-rtmp)
- [RFC 3986: URI generic syntax](https://www.rfc-editor.org/rfc/rfc3986) ·
  [section 3.2.1, user information](https://www.rfc-editor.org/rfc/rfc3986#section-3.2.1) ·
  [section 5, reference resolution](https://www.rfc-editor.org/rfc/rfc3986#section-5)
- [RFC 8216: HTTP Live Streaming](https://www.rfc-editor.org/rfc/rfc8216)
- [WHATWG: MIME Sniffing](https://mimesniff.spec.whatwg.org/) ·
  [Fetch: the `Request` constructor](https://fetch.spec.whatwg.org/#dom-request)
- [WICG: Direct Sockets](https://wicg.github.io/direct-sockets/)
- [librist](https://code.videolan.org/rist/librist)
- Apple: [`NSAllowsArbitraryLoadsForMedia`](https://developer.apple.com/documentation/bundleresources/information-property-list/nsapptransportsecurity/nsallowsarbitraryloadsformedia) ·
  [Preventing insecure network connections](https://developer.apple.com/documentation/security/preventing-insecure-network-connections) ·
  [TN3179: Understanding local network privacy](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy) ·
  [multicast entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.networking.multicast) ·
  [`NSLocalNetworkUsageDescription`](https://developer.apple.com/documentation/bundleresources/information-property-list/nslocalnetworkusagedescription)
- Android: [Network security configuration](https://developer.android.com/privacy-and-security/security-config) ·
  [`NetworkSecurityPolicy`](https://developer.android.com/reference/android/security/NetworkSecurityPolicy) ·
  [`WifiManager.MulticastLock`](https://developer.android.com/reference/android/net/wifi/WifiManager.MulticastLock)
- [ADR-0011](0011-sources-open-network-stream.md), [ADR-0014](0014-dependency-and-licensing-policy.md),
  [ADR-0015](0015-threading-and-task-runners.md), [ADR-0016](0016-penumbra-port-contract.md),
  [ADR-0017](0017-settings-schema-and-configuration.md), [ADR-0018](0018-error-model-and-recovery.md),
  [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md), [ADR-0021](0021-testing-strategy.md);
  [layering checks](../architecture/layering.md), [port contract](../architecture/port-contract.md)
