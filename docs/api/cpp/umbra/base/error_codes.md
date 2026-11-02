\var umbra::base::ErrorCode::kNetworkTimeout
`ALUMBRA-1001`: Network request timed out.

Category network, default severity recoverable, retryable, canonical code `Unavailable`, MediaError 2.

\var umbra::base::ErrorCode::kNetworkAccessDenied
`ALUMBRA-1002`: Network request denied.

Category network, default severity fatal, not retryable, canonical code `PermissionDenied`, MediaError 2.

\var umbra::base::ErrorCode::kNetworkRequestFailed
`ALUMBRA-1003`: Network request failed.

Category network, default severity fatal, not retryable, canonical code `Unavailable`, MediaError 2.

\var umbra::base::ErrorCode::kNetworkSchemeUnsupported
`ALUMBRA-1004`: URL scheme is not supported.

Category network, default severity fatal, not retryable, canonical code `Unimplemented`, MediaError 4.

\var umbra::base::ErrorCode::kNetworkUrlInvalid
`ALUMBRA-1005`: URL is invalid.

Category network, default severity fatal, not retryable, canonical code `InvalidArgument`, MediaError 4.

\var umbra::base::ErrorCode::kManifestInvalid
`ALUMBRA-2001`: Manifest is invalid.

Category manifest, default severity fatal, not retryable, canonical code `InvalidArgument`, MediaError 4.

\var umbra::base::ErrorCode::kMediaCorrupt
`ALUMBRA-3001`: Media data is corrupt.

Category demux, default severity fatal, not retryable, canonical code `DataLoss`, MediaError 3.

\var umbra::base::ErrorCode::kSourceTypeUnsupported
`ALUMBRA-3002`: Source type is not supported.

Category demux, default severity fatal, not retryable, canonical code `Unimplemented`, MediaError 4.

\var umbra::base::ErrorCode::kCodecUnsupported
`ALUMBRA-4001`: Codec is unsupported.

Category decode, default severity fatal, not retryable, canonical code `Unimplemented`, MediaError 4.

\var umbra::base::ErrorCode::kDecoderFallback
`ALUMBRA-4002`: Decoder requires a fallback.

Category decode, default severity degraded, not retryable, canonical code `FailedPrecondition`, MediaError 3.

\var umbra::base::ErrorCode::kDecoderInitializationFailed
`ALUMBRA-4003`: Video decoder failed to initialize.

Category decode, default severity fatal, not retryable, canonical code `Internal`, MediaError 3.

\var umbra::base::ErrorCode::kDrmLicenseDenied
`ALUMBRA-5001`: DRM licence denied.

Category drm, default severity fatal, not retryable, canonical code `PermissionDenied`, MediaError 3.

\var umbra::base::ErrorCode::kTextInvalid
`ALUMBRA-6001`: Caption data is invalid.

Category text, default severity degraded, not retryable, canonical code `InvalidArgument`, MediaError 3.

\var umbra::base::ErrorCode::kAdsUnavailable
`ALUMBRA-7001`: Ad source is unavailable.

Category ads, default severity degraded, not retryable, canonical code `Unavailable`, MediaError 2.

\var umbra::base::ErrorCode::kSettingsInvalid
`ALUMBRA-8001`: Setting value is invalid.

Category player, default severity fatal, not retryable, canonical code `InvalidArgument`, MediaError 4.

\var umbra::base::ErrorCode::kPlayerCancelled
`ALUMBRA-8002`: Player operation cancelled.

Category player, default severity recoverable, not retryable, canonical code `Cancelled`, MediaError 1.

\var umbra::base::ErrorCode::kPlayerInternal
`ALUMBRA-8003`: Player invariant failed.

Category player, default severity fatal, not retryable, canonical code `Internal`, MediaError 3.
