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

\class umbra::player::MediaError
A fatal error with its HTML code, message and structured detail.

\fn umbra::player::MediaError::MediaError(MediaErrorCode code, base::ErrorDetail detail)
Creates an error and builds its message from the detail.

The message is `ALUMBRA-` and the four-digit catalogue code, a colon and the catalogue description, for example
`ALUMBRA-3001: Media data is corrupt`.

\fn MediaErrorCode umbra::player::MediaError::code() const
Returns the HTML error code.

\fn const std::string& umbra::player::MediaError::message() const
Returns the message, which starts with the catalogue code.

\fn const base::ErrorDetail& umbra::player::MediaError::detail() const
Returns the structured detail: catalogue code, category, severity, retryable flag and redacted context.

This extension never changes the meaning of the HTML members.
