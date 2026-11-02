public enum ErrorCode: Int {
  case networkTimeout = 1001
  case networkAccessDenied = 1002
  case networkRequestFailed = 1003
  case networkSchemeUnsupported = 1004
  case networkUrlInvalid = 1005
  case sourceNotFound = 1006
  case sourceAccessDenied = 1007
  case sourceNotReadable = 1008
  case sourceReadFailed = 1009
  case networkServerError = 1010
  case networkTlsFailed = 1011
  case networkRedirectRefused = 1012
  case networkRangeUnsatisfiable = 1013
  case manifestInvalid = 2001
  case mediaCorrupt = 3001
  case sourceTypeUnsupported = 3002
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
