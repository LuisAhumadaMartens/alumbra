\file umbra/player/media_error.h
The fatal error of the media element.

HTML counterpart: `MediaError`. `code()` has the four HTML values, `message()` starts with the catalogue code
(`ALUMBRA-NNNN`), and `detail()` is the structured detail of
[ADR-0018](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/adr/0018-error-model-and-recovery.md).

\enum umbra::player::MediaErrorCode
The `MediaError` code.

HTML counterpart: the `MEDIA_ERR_*` constants, with the same numeric values. There is no zero value, because
Player::error() is empty when there is no error.

\var umbra::player::MediaErrorCode::kAborted
`MEDIA_ERR_ABORTED` (1): the user agent stopped fetching at the user's request.

\var umbra::player::MediaErrorCode::kNetwork
`MEDIA_ERR_NETWORK` (2): a network error stopped the fetch.

\var umbra::player::MediaErrorCode::kDecode
`MEDIA_ERR_DECODE` (3): the media was found corrupt or could not be decoded.

\var umbra::player::MediaErrorCode::kSrcNotSupported
`MEDIA_ERR_SRC_NOT_SUPPORTED` (4): no usable resource was found, or its format is not supported.

\fn constexpr MediaErrorCode umbra::player::MediaErrorCodeFor(base::ErrorCategory category)
Returns the fallback code of an error category.

Network and ads give `MEDIA_ERR_NETWORK`, manifest gives `MEDIA_ERR_SRC_NOT_SUPPORTED`, and demux, decode, DRM,
text and player give `MEDIA_ERR_DECODE`. The fallback applies only to codes the catalogue does not know.

\fn constexpr MediaErrorCode umbra::player::MediaErrorCodeFor(const base::ErrorDetail& detail)
Returns the code that the error catalogue declares for the detail's catalogue code.

Each catalogue code declares its `MediaError` code in the error catalogue reference. A code that the catalogue
does not know falls back to the code of its category. Before the metadata is known the element reports
`MEDIA_ERR_SRC_NOT_SUPPORTED` whatever the detail says, as the HTML media element failure steps require.

\class umbra::player::MediaError
A fatal error with its HTML code, message and structured detail.

\fn umbra::player::MediaError::MediaError(MediaErrorCode code, base::ErrorDetail detail)
Creates an error and builds its message from the detail.

The message is `ALUMBRA-` and the four-digit catalogue code, a colon and the catalogue description, for example
`ALUMBRA-3001: Media data is corrupt`. It is built from the catalogue only, never from the context, so it contains
no URL or other diagnostic value.

\fn MediaErrorCode umbra::player::MediaError::code() const
Returns the HTML error code.

\fn const std::string& umbra::player::MediaError::message() const
Returns the message, which starts with the catalogue code.

\fn base::ErrorCategory umbra::player::MediaError::category() const
Returns the category of the catalogue code: network, manifest, demux, decode, DRM, text, ads or player.

Alumbra extension.

\fn std::optional<int64_t> umbra::player::MediaError::platform_code() const
Returns the platform error code from the `platform_error_code` context field, or an empty value.

Alumbra extension.

\fn std::optional<int> umbra::player::MediaError::http_status() const
Returns the HTTP status from the `http_status` context field, or an empty value.

Alumbra extension.

\fn const base::ErrorDetail& umbra::player::MediaError::detail() const
Returns the structured detail: catalogue code, category, severity, retryable flag and redacted context.

This extension never changes the meaning of the HTML members.
