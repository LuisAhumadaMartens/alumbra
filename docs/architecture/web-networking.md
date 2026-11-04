# Web HTTP transport

The web Penumbra transport implements the internal `HttpTransport` interface
with the browser Fetch API. It is built as `penumbra::web_http` for the
Emscripten presets and uses the shared JavaScript fetch helper in
[`web/fetch_transport.mjs`](../../web/fetch_transport.mjs). Request and response
metadata cross the WASM boundary; response body chunks are copied into WASM
memory as the browser reader yields them, then retained by `SharedBytes` for
the duration of each callback.

The transport preserves HTTP status as a response, passes the final redirected
URL from `Response.url`, builds byte ranges as `Range` headers and streams each
body chunk. `AbortController` cancels an active fetch and its body reader.
Cancellation suppresses completion and error callbacks. `Pause` stops asking
the reader for another chunk and `Resume` continues it; the browser may already
have buffered a bounded amount in its stream queue.

Fetch does not expose a connect timeout. The transport applies the request's
`connect_timeout` as a deadline for receiving response headers, and Umbra's
network-sequence read timer owns the idle timeout after the response begins.
The browser follows redirects itself and applies its Fetch Standard redirect
limit; it does not expose per-request redirect-count control. The response's
final URL remains available for relative URL resolution. Fetch also does not
expose a reliable negotiated protocol or exact network arrival time for each
stream chunk. The transport uses Resource Timing's `nextHopProtocol` where the
browser exposes it and marks timing as coarse, so bandwidth estimation uses
whole-request timing.

## CORS and exposed headers

A rejected fetch `TypeError` is reported as `UNAVAILABLE` with a diagnostic
that identifies a probable CORS failure or network error. Fetch deliberately
does not reveal which of those browser security checks failed. A response with
an HTTP error status still produces `OnResponse` and `OnComplete`; Umbra's
recovery ladder classifies the status.

For a cross-origin response, JavaScript can read only CORS-safelisted response
headers and headers named by `Access-Control-Expose-Headers`. Servers must
expose non-safelisted fields that Umbra needs, including `Content-Range` and
`Date`. `Timing-Allow-Origin` controls whether Resource Timing exposes network
timing such as the negotiated protocol. Cross-origin credential mode `include`
also requires `Access-Control-Allow-Credentials: true` and an explicit
`Access-Control-Allow-Origin` value; a wildcard origin is not valid for that
mode.

The web-only settings are `network.http.credentials` (`omit`, `same-origin` or
`include`, default `same-origin`) and `network.http.use_priority_hints` (default
`true`). A request's explicit `include_credentials` flag opts that request into
`include`. Request hooks can change the URL and headers before
fetch, which supports host-provided authentication headers and URL signing.
Browsers may preflight cross-origin requests when hooks add non-safelisted
headers.

The fetch helper's Node tests use a mocked fetch and readable body. They verify
chunk delivery before completion, ranges, redirects, credentials, priority,
request hooks, CORS-style failures, cancellation, pause/resume and stop. They
do not establish browser conformance; Chromium, Firefox and WebKit require the
browser test environment described by the web conformance work.
