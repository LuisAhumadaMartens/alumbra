# Status, detail and diagnostic privacy

Fallible engine functions return `[[nodiscard]] absl::Status` or
`[[nodiscard]] absl::StatusOr<T>`. Umbra is built with `-fno-exceptions`.
The installed C++20 contract contains `ErrorDetail`, the generated catalogue
and `UrlRedactor`; Abseil-based helpers live in the non-installed
`umbra/base/internal_include/umbra/base/status.h`. Penumbra's lower layer does
not depend on Umbra: adaptation at that boundary belongs to its consumer.
MediaError mapping (#96) and recovery policy (#127) are not implemented here.
The installed types are documented in the
[C++ API reference](https://luisahumadamartens.github.io/alumbra/api/cpp/).

## Creating and propagating errors

```cpp
[[nodiscard]] absl::Status LoadSegment(const UrlRedactor& redactor) {
  return MakeError(ErrorCode::kNetworkTimeout, redactor,
                   {{"http_status", "503"}, {"request_url", segment_url}});
}

[[nodiscard]] absl::Status Decode(const UrlRedactor& redactor) {
  ALUMBRA_RETURN_IF_ERROR(LoadSegment(redactor));
  ALUMBRA_ASSIGN_OR_RETURN(auto frame, ReadFrame());
  return Consume(std::move(frame));
}
```

`MakeError` chooses the catalogue's canonical code and default severity and
retryability. Its message is static catalogue text prefixed with `ALUMBRA-NNNN`.
`AttachErrorDetail` attaches explicit runtime severity/retryability to an
existing non-OK status, redacting its message and the whole cause chain.
Unrelated payloads are preserved: their owning types must enforce their own
privacy policy. Do not put credentials in arbitrary status messages or foreign
payloads. The redactor recognises URLs, not secrets in arbitrary prose.

`GetErrorDetail` returns `StatusOr<optional<ErrorDetail>>`: an absent payload
is a successful empty optional; malformed detail or unknown catalogue codes are not a
success-shaped default. The private v1 wire format uses little-endian uint32
fields and length-prefixed strings, with a 64 KiB payload limit, 64 context
entries per node and 16 nodes per cause chain. Catalogue codes, categories,
severities, boolean values, duplicate/invalid keys and trailing bytes are
validated. A 16-byte private provenance authenticator follows the encoded
detail and counts toward the 64 KiB limit. Invalid input returns
`InvalidArgument` without echoing the input.
The wire payload is internal, not an SDK serialisation contract.

Context keys use lower snake case (dots and hyphens are also accepted), at most
128 bytes. Use `platform_error_code`, `http_status` and `request_url` for the
issue's platform/HTTP/URL fields. Context values must be trusted diagnostic
data, not arbitrary headers, response bodies or secret-bearing prose.
Causes are immutable shared detail values; attaching them makes a redacted
snapshot, independent of later source mutations. Decoded values privately
retain a snapshot of each context and an authenticated provenance proof over
the original encoded payload and status message. The redactor owns a separate
random 128-bit provenance key, independent of the URL correlation key. Two
domain-separated SipHash-2-4 outputs authenticate that proof; the URL digest
API cannot act as an oracle for this separate key.

Reattaching or wrapping an unchanged context with the same redactor verifies
the proof and preserves its original origin/digest, rather than hashing the
already-rendered diagnostic. Changing severity does not destroy context
provenance. Mutated contexts, forged payload proofs and another redactor's
values are sanitised as untrusted input. A rendered `[url:...]` marker alone
never grants trust. The original message is preserved only when its whole
payload/message proof verifies. Reading raw externally supplied payloads
validates structure, not provenance: verification happens at the typed writer
before emission.

`ALUMBRA_RETURN_IF_ERROR` is one statement and evaluates its argument once.
`ALUMBRA_ASSIGN_OR_RETURN` accepts an assignment target or a declaration,
evaluates the right side once, moves the value on success and preserves the
status on failure without evaluating the left side. It expands to several
statements so declarations remain in the caller's scope: always put it inside
braces, including `if`/`else` and loops. Parenthesise assignment targets that
contain commas; use a type alias for a declaration whose type contains commas.

Thread factories must not throw. `ThreadPool::Create` returns a structured
error and joins already-started workers if a factory returns a null handle.
The existing convenience constructor is for infallible factories and aborts
with a diagnostic on null; use `Create` when startup failure is recoverable.
The standard-library thread factory remains fail-fast on resource exhaustion
under the no-exceptions build, rather than promising portable OS error capture.

## Canonical conventions

These are value classifications, not retry/recovery policy. Runtime severity
may differ from the default when recovery succeeds or playback degrades.

| Category (range) | Situation | Canonical code | Example |
|------------------|-----------|----------------|---------|
| network (1xxx) | Transient request timeout | `Unavailable` | `NetworkTimeout`, HTTP 503 |
| manifest (2xxx) | Invalid manifest supplied to a parser | `InvalidArgument` | `ManifestInvalid`, malformed playlist |
| demux and media (3xxx) | Corrupt media bytes | `DataLoss` | `MediaCorrupt`, truncated media box |
| decode (4xxx) | Unsupported codec | `Unimplemented` | `CodecUnsupported`, unsupported codec profile |
| DRM (5xxx) | Licence access denied | `PermissionDenied` | `DrmLicenseDenied`, licence server rejection |
| text (6xxx) | Invalid optional captions | `InvalidArgument` | `TextInvalid`, malformed cue |
| ads (7xxx) | Ad source unavailable | `Unavailable` | `AdsUnavailable`, failed optional ad request |
| player, API and settings (8xxx) | Invalid setting | `InvalidArgument` | `SettingsInvalid`, value outside schema range |

Cancellation uses `Cancelled`; a violated engine invariant uses `Internal`.
HTTP 401/403 use `PermissionDenied` or `Unauthenticated`, never a retryable
timeout code. A temporarily unusable decoder uses `FailedPrecondition`;
initialisation failure uses `Internal`. The full
[generated catalogue](../reference/error-catalogue.md) records defaults.

## Shared URL redaction

Create one `UrlRedactor` per playback and share it with error producers and
the future logging facade (#50). The default constructor obtains a fresh
128-bit key from `std::random_device`; do not persist it or reuse a redactor
across playbacks. The explicit-key constructor is for tests or a caller-owned
ephemeral playback key, never a globally stable key. Const operations can run
concurrently and do not mutate state.

Default output is `https://origin:port [url:0123456789abcdef]`. HTTP(S) user
information, path, query and fragment never appear. Opaque, relative and
malformed URLs emit `[redacted-url]` plus the digest instead of guessing an
origin. Scheme case is normalised; the digest uses the complete original URL,
including credentials and fragments. Embedded absolute URLs in messages are
redacted too, including literal apostrophes in URI user information and paths.
URL-valued fields should have a `_url` or `_uri` key so relative
URLs are also recognised.

The implementation uses SipHash-2-4 with a 128-bit key and a 64-bit output,
verified against published vectors at message lengths 0, 1, 7, 8, 15, 16,
31, 32, 62 and 63. This is a correlation identifier, not encryption,
authorisation, or proof that a URL has no credentials. Collisions are possible;
a leaked key or side channel weakens protection. The algorithm is implemented
from its specification, without copying upstream implementation code or adding
a library dependency. See the [SipHash paper](https://131002.net/siphash/siphash.pdf)
and [published vectors](https://github.com/veorq/SipHash/blob/master/vectors.h).

`UrlDiagnostics::kDebugFullUrl` is an explicit debug opt-in, not a setting
that silently changes default diagnostics. Even then, only exact public
path/query-value/fragment literals registered in `DebugUrlAllowlist` can appear;
unlisted components are replaced with `[redacted]`. Query names resembling
credentials and configured token names remain prohibited even if allowlisted.
Percent-encoded query names are never eligible for debug allowlisting.
User information is always removed. The allowlist must contain only reviewed
noncredential literals (for example `/video/main.m3u8`, `quality=hd`, `chapter`),
never values learned from an untrusted URL. Empty allowlists fail closed.
This deliberately conservative opt-in can display a full known-safe URL while
refusing unclassified opaque tokens: no heuristic can reliably recognise them.

`RedactField` prohibits authentication, cookie, configured-token headers and
DRM/licence bodies in both modes. Header/body containers are dropped wholesale,
not parsed. Token-header matching ignores ASCII case and punctuation.
The error context writer uses this same primitive, including on nested causes.
Do not bypass it with raw `SetPayload`, format a raw URL into a message, or
register credential values in the debug allowlist.

## Catalogue maintenance

`errors/catalogue.toml` is the one source, using Python 3.11+ standard-library
`tomllib` like the settings schema. Each record declares code, category,
severity, retryability, canonical code, future MediaError code and plain text.
The generator rejects duplicates, wrong ranges, incomplete category coverage,
invalid metadata and unrecognised fields. Generated C++20 metadata, TypeScript,
Kotlin and Swift enums and reference Markdown are committed:

```sh
python3 tools/errors/generate.py
python3 tools/errors/generate.py --check
python3 tools/errors/generate_test.py
```

CI runs the drift check explicitly. Ordinary builds need no generator or
Python 3.11; CTest also runs generator checks when a suitable interpreter is
available. Do not edit generated files by hand. Released code meanings and
numbers must never change or be reused; this is a review invariant, not a
claim that a release-history registry already exists.
