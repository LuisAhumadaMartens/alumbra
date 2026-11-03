# Error catalogue

Generated from `errors/catalogue.toml` by `tools/errors/generate.py`.
Never renumber, reuse or change the meaning of a released code.
MediaError numbers are metadata for #96, not a mapping implementation.

| Code | Name | Category | Severity | Retryable | MediaError | Canonical | Example |
|------|------|----------|----------|-----------|------------|-----------|---------|
| 1001 | NetworkTimeout | network | recoverable | true | 2 | `Unavailable` | Network request timed out |
| 1002 | NetworkAccessDenied | network | fatal | false | 2 | `PermissionDenied` | Network request denied |
| 1003 | NetworkRequestFailed | network | fatal | false | 2 | `Unavailable` | Network request failed |
| 1004 | NetworkSchemeUnsupported | network | fatal | false | 4 | `Unimplemented` | URL scheme is not supported |
| 1005 | NetworkUrlInvalid | network | fatal | false | 4 | `InvalidArgument` | URL is invalid |
| 1006 | SourceNotFound | network | fatal | false | 4 | `NotFound` | Source does not exist |
| 1007 | SourceAccessDenied | network | fatal | false | 4 | `PermissionDenied` | Source cannot be read with the available permissions |
| 1008 | SourceNotReadable | network | fatal | false | 4 | `InvalidArgument` | Source is not a readable file |
| 1009 | SourceReadFailed | network | recoverable | false | 2 | `Unavailable` | Reading the source failed |
| 1010 | NetworkServerError | network | recoverable | true | 2 | `Unavailable` | Server is temporarily unable to serve the request |
| 1011 | NetworkTlsFailed | network | fatal | false | 2 | `Unavailable` | TLS connection could not be established or verified |
| 1012 | NetworkRedirectRefused | network | fatal | false | 2 | `FailedPrecondition` | Redirect was refused or exceeded the limit |
| 1013 | NetworkRangeUnsatisfiable | network | fatal | false | 2 | `OutOfRange` | Server cannot satisfy the requested byte range |
| 2001 | ManifestInvalid | manifest | fatal | false | 4 | `InvalidArgument` | Manifest is invalid |
| 3001 | MediaCorrupt | demux | fatal | false | 3 | `DataLoss` | Media data is corrupt |
| 3002 | SourceTypeUnsupported | demux | fatal | false | 4 | `Unimplemented` | Source type is not supported |
| 4001 | CodecUnsupported | decode | fatal | false | 4 | `Unimplemented` | Codec is unsupported |
| 4002 | DecoderFallback | decode | degraded | false | 3 | `FailedPrecondition` | Decoder requires a fallback |
| 4003 | DecoderInitializationFailed | decode | fatal | false | 3 | `Internal` | Video decoder failed to initialize |
| 4004 | CapabilityChanged | decode | degraded | true | 3 | `Unavailable` | Decoder or output capability changed during playback |
| 5001 | DrmLicenseDenied | drm | fatal | false | 3 | `PermissionDenied` | DRM licence denied |
| 6001 | TextInvalid | text | degraded | false | 3 | `InvalidArgument` | Caption data is invalid |
| 7001 | AdsUnavailable | ads | degraded | false | 2 | `Unavailable` | Ad source is unavailable |
| 8001 | SettingsInvalid | player | fatal | false | 4 | `InvalidArgument` | Setting value is invalid |
| 8002 | PlayerCancelled | player | recoverable | false | 1 | `Cancelled` | Player operation cancelled |
| 8003 | PlayerInternal | player | fatal | false | 3 | `Internal` | Player invariant failed |
