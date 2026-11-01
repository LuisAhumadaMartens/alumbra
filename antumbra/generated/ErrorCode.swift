public enum ErrorCode: Int {
  case networkTimeout = 1001
  case networkAccessDenied = 1002
  case networkRequestFailed = 1003
  case manifestInvalid = 2001
  case mediaCorrupt = 3001
  case codecUnsupported = 4001
  case decoderFallback = 4002
  case decoderInitializationFailed = 4003
  case drmLicenseDenied = 5001
  case textInvalid = 6001
  case adsUnavailable = 7001
  case settingsInvalid = 8001
  case playerCancelled = 8002
  case playerInternal = 8003
}
