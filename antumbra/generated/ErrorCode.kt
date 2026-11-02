package org.alumbra

enum class ErrorCode(val code: Int) {
  NetworkTimeout(1001),
  NetworkAccessDenied(1002),
  NetworkRequestFailed(1003),
  NetworkSchemeUnsupported(1004),
  NetworkUrlInvalid(1005),
  ManifestInvalid(2001),
  MediaCorrupt(3001),
  SourceTypeUnsupported(3002),
  CodecUnsupported(4001),
  DecoderFallback(4002),
  DecoderInitializationFailed(4003),
  DrmLicenseDenied(5001),
  TextInvalid(6001),
  AdsUnavailable(7001),
  SettingsInvalid(8001),
  PlayerCancelled(8002),
  PlayerInternal(8003),
}
