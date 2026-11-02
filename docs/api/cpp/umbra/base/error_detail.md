\file umbra/base/error_detail.h
Structured detail of an Umbra error.

Umbra attaches it to a non-OK `absl::Status`. The Abseil-based helpers that attach and read it, `MakeError`,
`AttachErrorDetail` and `GetErrorDetail`, are internal; see
[errors](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/errors.md).

\struct umbra::base::ErrorDetail
Catalogue code, runtime classification, diagnostic context and cause of an error.

Context keys are lower snake case, with dots and hyphens also accepted, and at most 128 bytes; use
`platform_error_code`, `http_status` and `request_url` for platform, HTTP and URL fields. Values must be trusted
diagnostic data, never arbitrary headers, response bodies or prose that may contain secrets. The error writer
redacts the context with UrlRedactor, including the context of every cause.

\fn umbra::base::ErrorDetail::ErrorDetail()
Creates the detail of an internal player error: ErrorCode::kPlayerInternal, fatal and not retryable, with no
context or cause.

\fn umbra::base::ErrorDetail::ErrorDetail(ErrorCode code, ErrorCategory category, ErrorSeverity severity, bool retryable, std::map<std::string, std::string> context, std::shared_ptr<const ErrorDetail> cause)
Creates a detail from explicit values.

\var ErrorCode umbra::base::ErrorDetail::code
Catalogue code; ErrorCode::kPlayerInternal by default.

\var ErrorCategory umbra::base::ErrorDetail::category
Category of the code; ErrorCategory::kPlayer by default.

\var ErrorSeverity umbra::base::ErrorDetail::severity
Severity at run time, which can differ from the catalogue default; ErrorSeverity::kFatal by default.

\var bool umbra::base::ErrorDetail::retryable
Whether retrying the failed operation can succeed; false by default.

\var std::map<std::string, std::string> umbra::base::ErrorDetail::context
Diagnostic context as key-value pairs.

\var std::shared_ptr<const ErrorDetail> umbra::base::ErrorDetail::cause
Immutable detail of the error that caused this one, or null.
