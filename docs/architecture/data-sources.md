# Data sources and the URL resolver

This page documents the layer that [ADR-0026](../adr/0026-network-stack-and-http-transport.md) and
[ADR-0027](../adr/0027-protocol-scope-and-url-resolution.md) decide and #73 builds: how Umbra reads bytes from
anywhere and how a URL becomes a source. The HTTP byte stream (#75), the `file` byte stream (#74) and the
manifest parsers (#76 onward) build on it. The port-side interfaces are in the
[port contract](port-contract.md#network-and-stream-providers); everything here is engine-internal, in
`umbra/source/internal_include/umbra/source/` (not installed), as the `umbra::source` target.

Every class here lives on **one sequence**, normally Umbra's network sequence (ADR-0015), and nothing blocks
it. Port callbacks may arrive on any platform thread; the in-tree receiver posts them to that sequence before
they reach any class below, so these classes never see another thread.

## URLs

`UrlReference` implements the RFC 3986 grammar and §5 reference resolution. HLS and DASH parsers resolve
relative references with the same code.

| Function | Behaviour |
|----------|-----------|
| `UrlReference::Parse` | Any URI reference, absolute or relative. The scheme and host are lower-cased; everything else is kept as written. An absent query or fragment is distinct from an empty one. |
| `UrlReference::ParseAbsolute` | The same, but a scheme is required. |
| `ResolveReference(base, reference)` | RFC 3986 §5.2 with the strict rule for a reference that has a scheme. The base must be absolute; its fragment is ignored. |
| `RemoveDotSegments` | RFC 3986 §5.2.4. |
| `ToString` | Serialises a reference. A path that starts with `//` and has no authority is printed as `/.//…`, so the text parses back to the same reference. |
| `Origin`, `PathExtension`, `WithoutUserInfo` | Helpers for credentials and type detection. |

The parser fails fast with `kInvalidArgument` on a control character, a space, a byte above 0x7F or any of
`"<>\^`{|}`, a malformed percent escape, a malformed scheme, host, IP literal or port, and a port above 65535.
Callers percent-encode such characters. The last `@` in the authority ends the user information, as cameras
need. The RFC 3986 §5.4 normal and abnormal examples run as unit tests, and `url_parser_fuzzer` checks that
parsing and printing are stable, that resolution of any base and reference succeeds and leaves no `.` or `..`
segment.

## Scheme registry

`SchemeRegistry` maps each scheme to the provider that the active port registered. `RegisterFrom` reads a port's
`StreamProviderCatalogue`; `RegisterByteStream` and `RegisterElementaryStream` register one provider.
Schemes are lower case and compare case-insensitively. Registration is all or nothing: an empty provider, an
invalid scheme or a scheme that already has a provider fails with `kInvalidArgument` or `kAlreadyExists` and
changes nothing. `http`, `https` and `file` are not special: #75 and #74 register their byte streams, and the
web port registers only `http` and `https`.

## Resolution

`SourceResolver::Resolve(url, options)` runs once per `load()` and does no I/O:

1. Parse an absolute URL. Failure gives `NetworkUrlInvalid` (1005).
2. Handle user information. By default `user:pass@` is percent-decoded into a `penumbra::Credential` for the
   URL's origin and stripped, so the resolved URL, every log and every error only ever contain the stripped URL.
   With `UserInfoPolicy::kReject`, which the web port uses, the URL fails with `NetworkUrlInvalid` and a message
   that points to the credential callback.
3. Find the scheme's provider. A scheme without one fails with `NetworkSchemeUnsupported` (1004), whose message
   and context name the scheme and the platform (`ResolveOptions::platform`), for example
   `rtmp://` on desktop before #231 registers a provider.
4. Reject SubRip. `srt://` always selects the SRT provider. A `.srt` path over `http`, `https` or `file`
   is a SubRip subtitle file, valid only as a text track (#134), and fails with `SourceTypeUnsupported` (3002)
   and a message that points to text tracks. An explicit type hint skips this check, because an explicit
   type is authoritative.

The result, `ResolvedSource`, carries the stripped URL, the provider kind (`ByteStream` or `ElementaryStream`),
the provider, the credential and the explicit type. `ParseTypeHint` converts the `source.type_hint` setting
(`auto` or a type name). All three new codes map to `MEDIA_ERR_SRC_NOT_SUPPORTED` (4) and carry an ADR-0018
`ErrorDetail` that the redactor has already processed.

`CredentialProvider` is the interface for the integrator's credential callback: it receives a
`CredentialChallenge` (origin, authentication scheme, realm) and answers asynchronously with an optional
credential. Credentials live in memory only. Wiring the callback to HTTP and RTSP challenges belongs to #75 and
#232.

## Source type detection

The resolver decides the type of a source in ADR-0027's order: an explicit type; the scheme (for the
transport only); a sniff of the first bytes of the response that is already being read; and the extension
and `Content-Type`, which only break ties.

`SniffSourceType(bytes, end_of_source, hints)` is a pure function over the bytes from offset zero:

| Result | Meaning |
|--------|---------|
| `kDecided` | A type and the deciding signal: `signature`, `extension` or `content_type`. |
| `kNeedMoreBytes` | The signature cannot be judged yet; `wanted_bytes` is the total the sniffer wants, always more than it has and at most 64 KiB. Never returned at the end of the source. |
| `kUndecided` | Neither signature nor tie-breaker decides. |

Signatures: `#EXTM3U` for HLS, an `MPD` root element for DASH, an ISO base media box (`ftyp`, `styp`, `moov`,
`moof`, `mdat`, `free`, `skip` or `wide`) for MP4, the EBML magic for Matroska and WebM, `FLV\x01`, and the
`0x47` sync byte at offsets 0, 188 and 376 for MPEG-TS. #76 and D-05 own the complete set. A UTF-8 byte-order mark
and leading whitespace are skipped. For XML, the declaration, processing instructions, comments and a
`DOCTYPE` are skipped to reach the root element; a root other than `MPD` is a decisive no, so an HTML error page
is never mistaken for a manifest by its `Content-Type`.

The budget is 4 KiB. It grows, doubling up to 64 KiB, only while an XML or text prologue hides the root element.
Extension and `Content-Type` apply only when no signature matched, and only to a type that fits the bytes: text
bytes can pick HLS or DASH, binary bytes the container types. The extension is consulted first.

`SourceTypeDetector` runs the loop over a `BufferedReader`: it reads 4 KiB at offset zero, sniffs, asks for more
while the sniffer wants more, and finishes with the type or a `SourceTypeUnsupported` error naming how many
bytes it inspected. Because the reader keeps bytes until a later read moves past them, the demuxer's first read
at offset zero is served from the same buffer, so nothing is fetched twice. `source_sniffer_fuzzer` checks the
need-more contract and that hints never override a signature.

## Data sources

`DataSource` is what #74 and #75 implement over a `ByteStream` provider: it reports `DataSourceInfo` (size if
known, seekability, content type and the final URL after redirects) and reads asynchronously at an offset.

| Rule | Detail |
|------|--------|
| `Read(offset, size, callback)` | Delivers up to `size` bytes. Fewer bytes than asked, or none, means the end of the source. A short read in the middle is a contract violation. Offsets at or past the end of a known-size source deliver an empty result. |
| Asynchronous | The callback never runs inside `Read`. It runs on the source's sequence. |
| Not seekable | Live sources accept only the next sequential offset and fail anything else with `kOutOfRange`. |
| Errors | Canonical `absl::Status` values with ADR-0018 details; the source does not retry. |
| Cancellation | `Read` returns a `ReadRequest`. `Cancel()` is idempotent, destroys the callback and guarantees it never runs, even when a result was already queued. |
| `Close()` | Cancels everything in flight and releases the connection. |

`ReadRequest::Complete` is how a source delivers a result: it returns false, without calling back, once the
request is cancelled or already completed. The fake source test cancels before delivery, after the result is
queued and from a racing thread, under TSan.

## Buffered reader

`BufferedReader` wraps a data source with read-ahead for the demuxers and the sniffer. It keeps at most
`source.read_ahead_bytes` (default 2 MiB, 64 KiB to 64 MiB) beyond the last read position, issues source reads
of `chunk_bytes` (64 KiB), clamps them to a known size and never asks for bytes it already holds. It also stops
asking once the window is full, which is where #75 pauses the transport (ADR-0026).

- One read can be outstanding. A second returns `kFailedPrecondition`; a cancelled one is replaced silently.
- A read behind the buffered window restarts the source at that offset for a seekable source and fails with
  `kOutOfRange` for a live one. A read far ahead of the window restarts a seekable source and otherwise reads
  forward, discarding.
- A failure of a read-ahead fetch is held until the next read that needs data, delivered once, and then a retry
  is allowed, so Umbra's recovery ladder (ADR-0018) stays the only retry policy.
- Results are delivered through the task runner, never inline, and the reader holds no references that a queued
  result needs, so destroying it leaves no callback behind.

## Follow-ups

- **#74 and #75** implement `DataSource` over the `file` and `http(s)` byte streams and register them. #75 adds the
  network-sequence receiver for the HTTP transport, pause and resume at the read-ahead limit, the idle-read timer
  and the credential callback for `401` challenges.
- **#76** owns the full signature set and `source.probe.max_bytes`.
- **#229** onward register the live providers and move SRT passphrases out of the URL into the credential.
