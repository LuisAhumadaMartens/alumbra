\file umbra/base/url_redactor.h
Redaction of URLs and credentials in diagnostics.

\include redact_urls.cc

\enum umbra::base::UrlDiagnostics
How much of a URL diagnostics can show.

\var umbra::base::UrlDiagnostics::kOriginAndDigest
Only the scheme, host and port, followed by a keyed digest of the whole URL; the default.

\var umbra::base::UrlDiagnostics::kDebugFullUrl
Explicit debug opt-in that also shows the path, query values and fragment registered in a DebugUrlAllowlist.

\struct umbra::base::DebugUrlAllowlist
Reviewed, non-credential URL components that UrlDiagnostics::kDebugFullUrl can show.

Unlisted components are replaced with `[redacted]`, and an empty allowlist shows none. Query names that look
like credentials, and configured token names, stay hidden even when listed. Register only reviewed literals,
never values taken from an untrusted URL.

\var std::set<std::string> umbra::base::DebugUrlAllowlist::paths
Exact paths that can appear, for example `/video/main.m3u8`.

\var umbra::base::DebugUrlAllowlist::query_values
Query parameter names, each with the exact values that can appear, for example `quality` with `hd`.

Percent-encoded names never qualify.

\var std::set<std::string> umbra::base::DebugUrlAllowlist::fragments
Exact fragments that can appear, for example `chapter`.

\class umbra::base::UrlRedactor
Removes credentials and private URL components from diagnostic text.

Create one redactor per playback and share it with error producers and logging. By default an HTTP or HTTPS URL
becomes its origin and a digest, such as `https://cdn.example.com [url:0123456789abcdef]`: user information, path,
query and fragment never appear. Opaque, relative and malformed URLs become `[redacted-url]` and the digest. The
digest is a 64-bit SipHash-2-4 correlation identifier keyed per redactor. It is not encryption, and it does not
prove that a URL is free of credentials. Const member functions can run concurrently.

\fn umbra::base::UrlRedactor::UrlRedactor()
Creates a redactor with fresh random 128-bit keys from `std::random_device`.

Do not persist the key or reuse a redactor across playbacks.

\fn umbra::base::UrlRedactor::UrlRedactor(std::array<std::uint64_t, 2> playback_key, const std::set<std::string>& token_headers = {}, UrlDiagnostics mode = UrlDiagnostics::kOriginAndDigest, DebugUrlAllowlist debug_allowlist = {})
Creates a redactor with an explicit digest key, extra token names, a diagnostics mode and a debug allowlist.

Use an explicit key only in tests or as a caller-owned ephemeral playback key, never as a globally stable key.
`token_headers` names further headers and query parameters whose values are credentials; matching ignores ASCII
case and punctuation. The key that authenticates error details is always random.

\fn std::string umbra::base::UrlRedactor::RedactUrl(std::string_view url) const
Returns the redacted form of one URL.

The digest covers the complete original URL, including credentials and fragment, so different URLs stay
distinguishable. In UrlDiagnostics::kDebugFullUrl mode, the allowlisted path, query values and fragment are kept
and every other component is replaced with `[redacted]`; user information is always removed.

\fn std::string umbra::base::UrlRedactor::RedactText(std::string_view text) const
Returns `text` with every embedded absolute URL redacted as RedactUrl() does.

It also replaces the value of a `name: value` header line or `name=value` pair whose name is a credential or a
configured token name with `[redacted]`: a header line keeps its value up to the end of the line. Credentials
that carry no such name cannot be recognised in prose, so never put them in diagnostic text.

\fn std::string umbra::base::UrlRedactor::RedactField(std::string_view key, std::string_view value) const
Returns the redacted value of a named diagnostic field.

Fields named for credentials, such as authorisation, cookie, password, secret, token, signature or licence
fields, configured token names, and any field whose name contains `header` or `body` become `[redacted]` in
every mode. Values of fields whose name contains
`url` or `uri` are redacted as URLs, so relative URLs are recognised too; other values are redacted as text.
