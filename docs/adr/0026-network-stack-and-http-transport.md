# 0026 — Fetch through each platform's HTTP stack behind one streaming transport interface

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #8

## Context

Manifests, segments, licences, ad requests and tracking beacons all arrive over HTTP. D-08 decides whether each
port fetches through its platform's HTTP stack or through one bundled stack, and what asynchronous interface
Umbra drives. The chunk timing that interface reports limits bandwidth estimation (D-11, #11), and with it
quality switching: in low-latency chunked CMAF, idle gaps between chunks dominate a segment's download time, so
whole-segment throughput says little about the link.

Each platform constrains the choice:

- **Web:** `fetch` is the only option. CORS applies, and request headers outside the CORS-safelisted set trigger
  a preflight.
- **Apple:** URLSession integrates App Transport Security, system proxies and VPNs, Low Data Mode, HTTP/3 and
  `URLSessionTaskMetrics`.
- **Android:** HttpEngine (the platform's Cronet), Cronet from Google Play Services or embedded, and OkHttp.
  Media3 ships a data source for each.
- **Desktop:** libcurl, with HTTP/2 through nghttp2.

Media3, Shaka Player and hls.js all keep retry policy above a pluggable transport. Cobalt is the counterpoint:
it brings Chromium's own network stack over sockets the port provides, which suits a browser-class runtime that
controls its environment better than a player embedded in third-party apps.

The v1 port contract ([port-contract.md](../architecture/port-contract.md)) deliberately declares no network
provider and leaves its shape to this decision. D-08 blocks the data-source and HTTP tasks (#73, #75) and the
network work of M4 to M7 (#150, #163, #189, #207).

Some parts are already settled and are not reopened here:

- [ADR-0018](0018-error-model-and-recovery.md) puts retries, backoff, failover and their `network.retry.*`
  settings in one Umbra component.
- [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md) fixes CMCD: v1 in header and query modes, off by
  default, only the applicable keys the player knows, a random `sid` per playback, and `cid` only when the
  integrator supplies one. CMCD v2 event reporting is deferred.
- ADR-0018 and ADR-0020 redact URLs, `Authorization` and cookie headers, and licence bodies.
- [ADR-0014](0014-dependency-and-licensing-policy.md) forbids FFmpeg from opening URLs or sockets.
- [ADR-0015](0015-threading-and-task-runners.md) delivers transport callbacks on Umbra's network sequence.
- Contract changes follow the `penumbra::kApiVersion` rule in port-contract.md.

## Decision drivers

- Respect the host platform: its proxies, VPNs, trust settings, data-saving modes, HTTP/3 and power policy.
- Keep mobile binaries small.
- Chunk arrival times precise enough for low-latency ABR, and bodies that reach demuxers without a copy.
- One owner for retries, request filters and CMCD, so behaviour is testable with a fake transport and recorded
  traces.
- One interface that runs unchanged on native threads and on the single-threaded web build.
- A contract every port can satisfy honestly, without inventing values its stack does not expose.
- Predictable and private behaviour: Alumbra is a player, not a platform, and persists nothing.
- Dependencies within ADR-0014's licence policy.

## Considered options

Stack (the issue's options):

1. **A — One bundled stack:** libcurl on every native platform and `fetch` on the web.
2. **B — Platform stacks** behind one Umbra transport interface.
3. **C — B plus app-supplied clients:** an app may hand over an existing OkHttp or URLSession client.

Transport interface:

1. **A — Asynchronous streaming:** pushed, timestamped chunks, with cancel, pause and resume.
2. **B — Pull-based reader:** the consumer asks for each next chunk.
3. **C — Whole bodies in M1**, with streaming added later.

Desktop transport:

1. **A — libcurl with OpenSSL 3** on macOS and Linux.
2. **B — URLSession on macOS** and libcurl on Linux.
3. **C — libcurl with mbedTLS.**

Narrower questions:

| Question | Chosen | Rejected |
|----------|--------|----------|
| Metrics every port reports | A small mandatory set, with a flag for coarse chunk timing | A full timing breakdown everywhere; bytes and duration only |
| Retries inside transports | Stale-connection replays only | No transport retries at all; retries of idempotent requests |
| Cookies and credentials | Stateless, with explicit opt-in | An in-memory cookie jar per player; the platforms' default stores |
| Default CMCD transmission | Query on the web, headers on native platforms | Query everywhere; headers everywhere |
| Prioritisation | Umbra request classes | First in, first out until ABR lands; a separate connection pool per class |
| Android default stack | HttpEngine, then Cronet from Play Services, then HttpURLConnection | Embedded Cronet; OkHttp |

## Decision

Chosen options: **stack C, staged** (platform stacks from M1, app-supplied clients with the Kotlin and Swift
SDKs), **interface A**, **desktop A**, and the choices in the table above. Each port implements one streaming
transport interface over its platform's stack, and Umbra keeps all policy above it. Platform stacks bring the operating system's networking behaviour for free and keep mobile binaries small.
The streaming interface is the only shape that serves low-latency ABR and zero-copy demuxing from the start.
One desktop transport is the quickest reference to build and the easiest to reason about.

### Stack per platform

| Platform | Transport | Work |
|----------|-----------|------|
| macOS and Linux | libcurl with OpenSSL 3 and nghttp2 | M1, #75 |
| Windows, if D-35 (#35) brings it in | libcurl with Schannel | With D-35 |
| Web | `fetch` with streamed response bodies | M4, #150 |
| Android and Google TV | HttpEngine, otherwise Cronet from Google Play Services, otherwise HttpURLConnection | M6, #189 |
| iOS, iPadOS and tvOS | URLSession | M7, #207 |

- Android uses HttpEngine on API 34 and later, and on API 31 and later with SDK extension 7. Without it, the port
  uses Cronet from Google Play Services, and without Play Services, as on AOSP TVs, HttpURLConnection. Nothing is
  embedded, so Alumbra adds no network stack to the APK; embedded Cronet would add about 8 MB. M6 confirms this
  default (#189).
- The Kotlin and Swift SDKs (D-18, #18) let an app supply its own client, such as an OkHttp instance with its
  interceptors or a configured URLSession, as Media3's `DataSource.Factory` does. An adapter wraps it in the same
  interface. The app then owns what its client does, such as its own retries, cookies and certificate pinning,
  and the SDK documentation states which of the guarantees below no longer hold.

### Desktop transport and TLS

- libcurl, driven through its multi interface, is the only desktop transport on macOS and Linux. It uses
  OpenSSL 3 (`Apache-2.0`) for TLS and nghttp2 (`MIT`) for HTTP/2. These licences and curl's own
  (`curl AND ISC AND BSD-3-Clause` in vcpkg) are all on ADR-0014's automatic list.
- Certificates are verified against the operating system's trust store. On Linux this is the distribution's CA
  bundle, which #75 locates at run time rather than relying on a default path compiled into the build.
- On macOS, OpenSSL does not read the Keychain, and libcurl ignores the proxies and proxy auto-configuration set
  in System Settings; it honours only proxy environment variables. Since curl 8.17.0, libcurl can hand
  certificate verification to Apple's SecTrust service, and vcpkg's curl port at Alumbra's baseline (curl 8.23.0)
  enables this for OpenSSL on Apple targets. Setting a CA file bypasses SecTrust, so #75 confirms the trust path
  of the pinned build and sets no CA file on macOS. System proxy settings stay unused until a task passes them to
  libcurl.
- Alumbra tracks OpenSSL security releases and keeps the pinned series supported. vcpkg's port at the baseline
  carries OpenSSL 3.6.5. The 3.6 series reaches its end of support on 2026-11-01, and 3.5 is the long-term
  support series until 2030-04-08. #75 pins a supported series through a baseline update or a recorded
  `overrides` entry (ADR-0014) before the desktop transport ships.
- Other TLS backends are excluded. curl 8.15.0 removed Apple's Secure Transport backend. wolfSSL is GPL, which
  ADR-0014 denies. GnuTLS is LGPL, so it would need ADR-0014's conditional review and replaceable shared-library
  packaging, which an Apache-2.0 library avoids.
- If D-35 (#35) brings Windows in, libcurl uses Schannel there, which is the default TLS choice of vcpkg's port on
  Windows. Windows then uses its own trust store and needs no OpenSSL.
- HTTP/3 is not part of the desktop transport. curl supports it through ngtcp2 and nghttp3, which would need their
  own review.

### Transport interface

The interface joins the Penumbra port contract, next to the other port interfaces in the non-installed
`internal_include/penumbra/interface/`. The names below show the shape; #73 fixes them.

- `Start(request, callbacks)` returns a request handle with `Cancel()`, `Pause()` and `Resume()`.
- A request carries the method, URL, headers, an optional byte range, an optional body as `SharedBytes` (for
  licence requests and beacons), its request class, a priority hint, a credentials opt-in for the web, the
  connect timeout and #75's redirect rules.
- Callbacks deliver the response head (status, headers, protocol version and the final URL after redirects),
  then each body chunk as `SharedBytes` with its monotonic arrival time, then either completion with metrics or
  an error. Chunks reach demuxers without a copy.
- A port may call back from any platform thread. As `CallbackDispatcher` does for the control sequence, the
  in-tree receiver posts each callback to Umbra's network sequence (ADR-0015). Arrival times are taken when the
  stack hands over the bytes, before the post, so queueing delay never enters a bandwidth sample. They use the
  monotonic clock the task runners use, so recorded traces replay on virtual time in tests. On the web, `fetch`
  callbacks and the network sequence share the browser's event loop.
- `Cancel()` is idempotent and nonblocking. Umbra discards any callback that arrives after it, as it discards
  stale-ticket callbacks, and a port makes no callbacks after its stop completes.
- `Pause()` stops reading from the connection, as far as the stack allows, so flow control holds the server back
  instead of the port buffering the body. `Resume()` continues. Umbra pauses a progressive download when its
  read-ahead is full, and bandwidth estimation excludes paused intervals.
- An HTTP error status is a response, not a transport error: Umbra's recovery component decides what a 404 or 503
  means (ADR-0018). Transport failures are canonical `absl::Status` values whose cause (DNS, connection, TLS,
  timeout or reset) the recovery ladder can classify, and Umbra maps them to the 1xxx catalogue codes.
- Transports keep no retry or policy state. Request filters, CMCD, retries, failover and timeouts belong to Umbra.
  Umbra enforces #75's idle-read timeout with a timer on the network sequence and cancels the request. Only the
  connect timeout travels in the request, because only the stack can observe connection setup; where a stack
  exposes no connect timeout, as with `fetch`, the read timeout covers it.
- Transports follow redirects themselves, since `fetch` cannot hand them back, and report the final URL. Each
  applies #75's redirect limit and its refusal of HTTPS-to-HTTP redirects as far as its stack allows.
- Each port provides the transport through a new provider in the port contract. Its pure virtual methods
  increment `penumbra::kApiVersion`, as the compatibility rule requires.

### Metrics and coarse timing

Every port reports, for every request:

- the response start time (time to first byte);
- the arrival time and size of each chunk;
- the completion time and the total body bytes;
- the HTTP status, protocol version and final URL.

DNS, connect and TLS times are optional, reported where the stack exposes them, as libcurl and
`URLSessionTaskMetrics` do. An unavailable value is reported as unknown, never as a guessed zero. The web port
reads the protocol version from Resource Timing, which hides it for a cross-origin response without
`Timing-Allow-Origin`.

A port whose chunks do not match network arrival reports `timing = coarse` in its metrics. `fetch` is such a
port: a stream chunk reflects how the browser buffered the body, not when the bytes arrived. With coarse timing,
bandwidth estimation (D-11, #11) uses whole-request samples instead: total bytes over the time from response
start to completion, so time to first byte is excluded.

### Retries inside transports

- Umbra owns every policy retry (ADR-0018).
- A transport may transparently replay a request that failed on a reused keep-alive connection before any
  response byte arrived. URLSession and Cronet do this anyway, and libcurl replays such a request on a fresh
  connection. A replay does not count as an attempt.
- Every other built-in retry is disabled where the stack allows it. A port whose stack retries in a way it cannot
  turn off documents the behaviour.

### Cookies and credentials

- Native transports keep no cookie jar: they store no `Set-Cookie` response and send no stored cookie.
- Authentication goes through Umbra's request filters, such as tokens, URL signing and licence wrapping (D-27,
  #27), and through the credential callback that D-14 (#14) defines.
- On the web, requests use `credentials: "same-origin"`, which is `fetch`'s default. An integrator can opt in to
  `include` per request type, for example for licence requests; the server must then answer with
  `Access-Control-Allow-Credentials` and an explicit origin.
- Alumbra persists nothing. Native transports use no persistent cookie, credential or response storage, such as
  an ephemeral URLSession configuration.
- A CDN that authenticates with cookies needs explicit setup: a request filter that adds the `Cookie` header on
  native platforms, or the `include` opt-in on the web.

### CMCD transmission

- CMCD stays off unless enabled (ADR-0020). When it is enabled, Umbra serialises it in a request filter.
- The default mode is query arguments on the web. CMCD's request headers are not CORS-safelisted, so header mode
  would cost a preflight on every cross-origin request.
- The default mode is headers on native platforms, which leaves CDN cache keys untouched.
- Integrators can choose either mode on any platform.

### Request classes and prioritisation

- Umbra tags each request with a class: manifest, licence, init (initialisation segments), media or prefetch.
- Umbra caps concurrency per class and cancels superseded work, such as media requests for a position a seek has
  left or a live manifest reload that a newer one replaces. Seeks and live reloads never queue behind prefetch.
- Transports receive a priority hint where it is cheap, such as `fetch`'s `priority` option or libcurl's HTTP/2
  stream weights. Connections are reused within each stack's own pool; there is no separate pool per class.
- M1 implements only the classes and cancel-on-supersede (#73, #75). Concurrency caps and priority hints arrive
  with the segment scheduler (#103).

### Settings

- CMCD uses `network.cmcd.enabled` (boolean, default `false`) and `network.cmcd.mode` (enum, `header` or
  `query`). The keys belong to the `network` namespace: `settings/schema/` holds one file per namespace, a key's
  first component must match its file, and `network` owns transport behaviour, including request serialisation.
  ADR-0017's `cmcd.enabled` illustrates the positive-boolean rule; it is not a key.
- Platforms never appear in keys (ADR-0017), so the per-platform default uses ADR-0017's port-default layer: the
  compiled default is `header`, and the web port's default is `query`.
- Timeouts use #75's keys, `network.http.connect_timeout_ms` and `network.http.read_timeout_ms`.

## Consequences

**Positive**
- On mobile and the web, each platform's proxies, VPNs, data-saving modes, trust settings and HTTP/3 come for
  free, and mobile binaries carry no network stack of Alumbra's.
- Low-latency ABR gets exact chunk timing wherever the stack provides it, and demuxers receive bodies without a
  copy.
- Retries, filters and CMCD live in one place and are tested once, against a fake transport and recorded traces.
- The same interface runs on native threads and on the single-threaded web build.
- Nothing is persisted, and requests carry no stored cookies unless an integrator asks.

**Negative / costs**
- What each port can observe differs: `fetch` timing is coarse, so low-latency ABR is weaker on the web, and DNS,
  connect and TLS times are optional.
- Every port must implement pause and resume.
- On macOS, the desktop transport ignores system proxy settings, and its trust path depends on how libcurl is
  built.
- Alumbra tracks OpenSSL security releases for desktop builds.
- Stale-connection replays are invisible to statistics.
- CDNs that authenticate with cookies need explicit setup, and two CMCD defaults must be documented.
- Android TVs without Google services fall back to HttpURLConnection, the weakest stack.
- App-supplied clients widen the test surface, and Alumbra cannot guarantee their behaviour.
- Umbra owns a small request scheduler.

**Follow-ups**
- **#73:** adds the transport interfaces to the Penumbra interface and to
  [port-contract.md](../architecture/port-contract.md), replacing its network-provider note, increments
  `penumbra::kApiVersion`, and builds the data source and a fake transport on them.
- **#75:** the libcurl transport with OpenSSL 3 and nghttp2, the Linux and macOS trust paths, a supported OpenSSL
  series, the mandatory metrics, request classes and cancel-on-supersede.
- **#71:** transport conformance cases for cancel and stop barriers, pause and resume, monotonic arrival times and
  the declared timing precision.
- **#103:** per-class concurrency caps and priority hints. **#127:** retries and failover over the transport,
  where stale-connection replays are not attempts.
- **D-11 (#11), #119 and #220:** bandwidth estimation from chunk timing, with the whole-request fallback for coarse
  ports. #122 replays recorded traces through the fake transport.
- **D-14 (#14):** the credential callback, and non-HTTP I/O, which stays outside this HTTP-only interface.
- **CMCD:** a network task, to be created, implements CMCD v1 serialisation and adds `network.cmcd.enabled` and
  `network.cmcd.mode` to `settings/schema/network.toml`, with `query` as the web port's default.
- **M4 (#150):** the `fetch` transport, with streamed bodies, coarse timing, Resource Timing metrics, the
  credentials opt-in and the `priority` hint.
- **M5 (#163):** ad requests and tracking beacons through the same transport, mapped onto the request classes.
- **M6 (#189):** the HttpEngine, Cronet and HttpURLConnection chain; confirm the default and the zero-size budget.
- **M7 (#207):** the URLSession transport, with an ephemeral configuration and `URLSessionTaskMetrics`.
- **D-18 (#18):** app-supplied clients in the Kotlin and Swift SDKs, and the public shape of request filters.
- **D-35 (#35):** the Schannel build, if Windows is brought in.

## Pros and cons of the options

### Stack A. One bundled stack

- Good, because behaviour, HTTP features and bugs are identical across native platforms.
- Bad, because it bypasses App Transport Security, system proxy handling and Low Data Mode on Apple platforms.
- Bad, because it grows mobile binaries and makes Alumbra own TLS roots and their updates.

### Stack B. Platform stacks

- Good, because operating-system proxies, VPNs, Low Data Mode, HTTP/3 and power behaviour come for free.
- Good, because the test surface stays bounded.
- Bad, because enterprise apps cannot reuse their interceptors, certificate pinning or authentication.

### Stack C. Platform stacks plus app-supplied clients

- Good, because it keeps B's platform integration and lets apps reuse the clients they already configure.
- Bad, because what Alumbra can observe and guarantee then depends on the app's client, and the test surface is
  unbounded.

### Interface A. Asynchronous streaming

- Good, because chunk timing serves low-latency ABR and chunks reach demuxers without a copy.
- Good, because it runs on a single thread on the web and is testable with a fake transport and recorded traces.
- Bad, because every port must implement pause and resume.

### Interface B. Pull-based reader

- Good, because backpressure is natural.
- Bad, because every chunk costs an extra task hop, and arrival times are measured at read time unless they are
  buffered with the data.

### Interface C. Whole bodies in M1

- Good, because it is the quickest to build.
- Bad, because large progressive files and low-latency CMAF need streaming, so every port's interface would change
  later.

### Desktop A. libcurl with OpenSSL 3

- Good, because one reference transport, in C and C++ throughout, gives exact chunk timing.
- Bad, because on macOS it ignores system proxies, and Keychain trust depends on the SecTrust build option.
- Bad, because Alumbra tracks OpenSSL security updates.

### Desktop B. URLSession on macOS

- Good, because macOS gets native trust, proxies and HTTP/3, and the Apple port (M7) reuses the code.
- Bad, because two desktop transports make the reference port behave differently per operating system.

### Desktop C. libcurl with mbedTLS

- Good, because the TLS library is smaller and available under Apache-2.0.
- Bad, because it has fewer features, and HTTP/3 would need the OpenSSL and ngtcp2 route anyway.

### Narrower questions

- **Metrics.** The small mandatory set lets every port comply honestly. A full breakdown everywhere would force
  some ports to fake values, and bytes and duration alone are useless for chunked low-latency CMAF.
- **Transport retries.** Stale-connection replays are enforceable on every stack and keep ADR-0018's single owner.
  Forbidding all retries is unenforceable on URLSession and Cronet, and stale connections would cause spurious
  failures. Idempotent retries in transports would stack a second retry layer under ADR-0018's and multiply
  delays.
- **Cookies.** Stateless requests are predictable and private. An in-memory jar would behave differently from
  the web, and the platforms' default stores bring persistence and cross-app leakage outside Alumbra's control.
- **CMCD.** Query mode everywhere can fragment CDN caches unless the CDN strips the arguments. Header mode
  everywhere costs every cross-origin web request a preflight.
- **Prioritisation.** First in, first out needs no M1 work, but from M2 seeks would queue behind prefetch.
  Separate pools per class isolate traffic at the cost of more connections and TLS handshakes.
- **Android.** Embedded Cronet gives the same stack everywhere at about 8 MB per app. OkHttp is familiar and has
  interceptors, but lacks HTTP/3 and brings Kotlin and Okio dependencies.

## References

- [D-08 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/8)
- [ADR-0014](0014-dependency-and-licensing-policy.md), [ADR-0015](0015-threading-and-task-runners.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0017](0017-settings-schema-and-configuration.md),
  [ADR-0018](0018-error-model-and-recovery.md), [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md)
  and the [Penumbra interface v1](../architecture/port-contract.md).
- [WHATWG Fetch Standard](https://fetch.spec.whatwg.org/):
  [CORS-safelisted request headers](https://fetch.spec.whatwg.org/#cors-safelisted-request-header) and
  [credentials mode](https://fetch.spec.whatwg.org/#concept-request-credentials-mode);
  [WHATWG Streams Standard](https://streams.spec.whatwg.org/).
- [MDN: `RequestInit` (`credentials` and `priority`)](https://developer.mozilla.org/en-US/docs/Web/API/RequestInit),
  [MDN: `nextHopProtocol` and `Timing-Allow-Origin`](https://developer.mozilla.org/en-US/docs/Web/API/PerformanceResourceTiming/nextHopProtocol),
  [W3C: Resource Timing](https://www.w3.org/TR/resource-timing/).
- Apple: [URLSessionTaskMetrics](https://developer.apple.com/documentation/foundation/urlsessiontaskmetrics),
  [allowsConstrainedNetworkAccess](https://developer.apple.com/documentation/foundation/urlsessionconfiguration/allowsconstrainednetworkaccess)
  and [ephemeral configuration](https://developer.apple.com/documentation/foundation/urlsessionconfiguration/ephemeral).
- Android: [HttpEngine](https://developer.android.com/reference/android/net/http/HttpEngine),
  [Cronet](https://developer.android.com/develop/connectivity/cronet) and
  [Media3 network stacks](https://developer.android.com/media/media3/exoplayer/network-stacks).
- curl: [multi interface](https://curl.se/libcurl/c/libcurl-multi.html),
  [`curl_easy_pause`](https://curl.se/libcurl/c/curl_easy_pause.html),
  [8.15.0 changes (Secure Transport removed)](https://curl.se/ch/8.15.0.html),
  [SSL certificate verification (Apple SecTrust)](https://curl.se/docs/sslcerts.html),
  [install options (`--with-apple-sectrust`, since 8.17.0)](https://curl.se/docs/install.html),
  [HTTP/3](https://curl.se/docs/http3.html) and
  [`Curl_retry_request`](https://github.com/curl/curl/blob/master/lib/transfer.c).
- vcpkg at `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`: curl
  [manifest](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/curl/vcpkg.json)
  and [portfile](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/curl/portfile.cmake),
  OpenSSL [manifest](https://github.com/microsoft/vcpkg/blob/9a5dfef5d1a0438a7d24856a85bd60f309d355e9/ports/openssl/vcpkg.json).
- [OpenSSL release strategy](https://openssl-library.org/policies/releasestrat/index.html).
- [CTA-5004-B: Common Media Client Data](https://cta-wave.github.io/Resources/common-media-client-data--cta-5004-b.html)
  and the [CTA-WAVE CMCD repository](https://github.com/cta-wave/common-media-client-data).
- Prior art: [Shaka Player networking](https://shaka-player-demo.appspot.com/docs/api/tutorial-network-and-buffering-config.html),
  [hls.js API](https://github.com/video-dev/hls.js/blob/master/docs/API.md) and
  [Cobalt `net/`](https://github.com/youtube/cobalt/tree/main/net).
