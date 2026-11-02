\file umbra/base/error_catalogue.h
Public error catalogue, generated from `errors/catalogue.toml`.

Codes are grouped by category in thousands. A released code is never renumbered, reused or given a new
meaning. The [generated catalogue](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/reference/error-catalogue.md)
lists every code with its defaults.

\include look_up_errors.cc

\enum umbra::base::ErrorCategory
Area of an error, which sets the thousands digit of its code.

\var umbra::base::ErrorCategory::kNetwork
Network requests and responses, codes 1000 to 1999.

\var umbra::base::ErrorCategory::kManifest
Manifests, codes 2000 to 2999.

\var umbra::base::ErrorCategory::kDemux
Demuxing and media data, codes 3000 to 3999.

\var umbra::base::ErrorCategory::kDecode
Decoding, codes 4000 to 4999.

\var umbra::base::ErrorCategory::kDrm
Content protection and licences, codes 5000 to 5999.

\var umbra::base::ErrorCategory::kText
Text tracks and captions, codes 6000 to 6999.

\var umbra::base::ErrorCategory::kAds
Advertising, codes 7000 to 7999.

\var umbra::base::ErrorCategory::kPlayer
Player, API and settings, codes 8000 to 8999.

\enum umbra::base::ErrorSeverity
Effect of an error on playback.

The catalogue records a default; the severity at run time can differ when recovery succeeds or playback
degrades.

\var umbra::base::ErrorSeverity::kRecoverable
Playback can continue after recovery, for example a retry.

\var umbra::base::ErrorSeverity::kDegraded
Playback continues with reduced function, for example without captions or ads.

\var umbra::base::ErrorSeverity::kFatal
Playback cannot continue.

\enum umbra::base::ErrorCode
Stable numeric code of a catalogue error, shown in messages as `ALUMBRA-` followed by the number.

\struct umbra::base::ErrorCatalogueEntry
Metadata of one catalogue error.

\var ErrorCode umbra::base::ErrorCatalogueEntry::code
Numeric code.

\var std::string_view umbra::base::ErrorCatalogueEntry::name
Name of the code without the `k` prefix, for example `NetworkTimeout`.

\var ErrorCategory umbra::base::ErrorCatalogueEntry::category
Category, which matches the thousands digit of the code.

\var ErrorSeverity umbra::base::ErrorCatalogueEntry::severity
Default severity.

\var bool umbra::base::ErrorCatalogueEntry::retryable
Whether retrying the failed operation can succeed, by default.

\var int umbra::base::ErrorCatalogueEntry::media_error
`HTMLMediaElement` `MediaError` code, from 1 to 4, recorded as metadata until the mapping is implemented.

\var std::string_view umbra::base::ErrorCatalogueEntry::canonical
Name of the canonical Abseil status code, for example `Unavailable`.

\var std::string_view umbra::base::ErrorCatalogueEntry::description
Static description used in status messages; it never contains runtime data.

\var umbra::base::kErrorCatalogue
Every catalogue entry, sorted by code.

\fn constexpr const ErrorCatalogueEntry* umbra::base::FindError(ErrorCode code)
Returns the catalogue entry for `code`, or null when this catalogue does not define it.

It can run in constant expressions.
