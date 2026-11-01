# Error catalogue

Generated from `errors/catalogue.toml` by `tools/errors/generate.py`.
Never renumber, reuse or change the meaning of a released code.
MediaError numbers are metadata for #96, not a mapping implementation.

| Code | Name | Category | Severity | Retryable | MediaError | Canonical | Example |
|------|------|----------|----------|-----------|------------|-----------|---------|
| 1001 | NetworkTimeout | network | recoverable | true | 2 | `Unavailable` | Network request timed out |
| 1002 | NetworkAccessDenied | network | fatal | false | 2 | `PermissionDenied` | Network request denied |
| 1003 | NetworkRequestFailed | network | fatal | false | 2 | `Unavailable` | Network request failed |
| 2001 | ManifestInvalid | manifest | fatal | false | 4 | `InvalidArgument` | Manifest is invalid |
| 3001 | MediaCorrupt | demux | fatal | false | 3 | `DataLoss` | Media data is corrupt |
| 4001 | CodecUnsupported | decode | fatal | false | 4 | `Unimplemented` | Codec is unsupported |
| 4002 | DecoderFallback | decode | degraded | false | 3 | `FailedPrecondition` | Decoder requires a fallback |
| 4003 | DecoderInitializationFailed | decode | fatal | false | 3 | `Internal` | Video decoder failed to initialize |
| 5001 | DrmLicenseDenied | drm | fatal | false | 3 | `PermissionDenied` | DRM licence denied |
| 6001 | TextInvalid | text | degraded | false | 3 | `InvalidArgument` | Caption data is invalid |
| 7001 | AdsUnavailable | ads | degraded | false | 2 | `Unavailable` | Ad source is unavailable |
| 8001 | SettingsInvalid | player | fatal | false | 4 | `InvalidArgument` | Setting value is invalid |
| 8002 | PlayerCancelled | player | recoverable | false | 1 | `Cancelled` | Player operation cancelled |
| 8003 | PlayerInternal | player | fatal | false | 3 | `Internal` | Player invariant failed |
