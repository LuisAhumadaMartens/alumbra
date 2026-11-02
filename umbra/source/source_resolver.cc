#include "umbra/source/source_resolver.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/credentials.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_error.h"
#include "umbra/source/source_type.h"
#include "umbra/source/url.h"

namespace umbra::source {
namespace {

bool CarriesFiles(std::string_view scheme) {
  return scheme == "http" || scheme == "https" || scheme == "file";
}

}

absl::StatusOr<std::optional<SourceType>> ParseTypeHint(
    std::string_view setting) {
  if (setting == "auto") {
    return std::optional<SourceType>();
  }
  const std::optional<SourceType> type = ParseSourceType(setting);
  if (!type.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown source.type_hint '", setting, "'"));
  }
  return type;
}

SourceResolver::SourceResolver(
    std::shared_ptr<const SchemeRegistry> registry,
    std::shared_ptr<const base::UrlRedactor> redactor)
    : registry_(std::move(registry)), redactor_(std::move(redactor)) {}

absl::StatusOr<ResolvedSource> SourceResolver::Resolve(
    std::string_view url, const ResolveOptions& options) const {
  absl::StatusOr<UrlReference> parsed = UrlReference::ParseAbsolute(url);
  if (!parsed.ok()) {
    return MakeSourceError(
        base::ErrorCode::kNetworkUrlInvalid, absl::StatusCode::kInvalidArgument,
        "the source URL is not a valid absolute URL", {}, *redactor_);
  }
  ResolvedSource resolved;
  resolved.explicit_type = options.type_hint;
  if (parsed->userinfo.has_value()) {
    if (options.user_info == UserInfoPolicy::kReject) {
      return MakeSourceError(
          base::ErrorCode::kNetworkUrlInvalid,
          absl::StatusCode::kInvalidArgument,
          "user information in a URL is not accepted on this platform; "
          "provide credentials through the credential callback",
          {{"platform", options.platform}}, *redactor_);
    }
    absl::StatusOr<penumbra::Credential> credential =
        CredentialFromUserInfo(*parsed->userinfo);
    if (!credential.ok()) {
      return MakeSourceError(
          base::ErrorCode::kNetworkUrlInvalid,
          absl::StatusCode::kInvalidArgument,
          "the user information in the source URL is invalid", {}, *redactor_);
    }
    resolved.credential = SourceCredential{
        .origin = parsed->Origin(), .credential = *std::move(credential)};
    parsed = parsed->WithoutUserInfo();
  }
  const std::string scheme = parsed->scheme.value_or(std::string());
  const SchemeEntry* entry = registry_->Find(scheme);
  if (entry == nullptr) {
    return MakeSourceError(
        base::ErrorCode::kNetworkSchemeUnsupported,
        absl::StatusCode::kUnimplemented,
        absl::StrCat("the '", scheme, "' scheme is not supported on ",
                     options.platform),
        {{"scheme", scheme}, {"platform", options.platform}}, *redactor_);
  }
  if (CarriesFiles(scheme) && !options.type_hint.has_value() &&
      parsed->PathExtension() == "srt") {
    return MakeSourceError(
        base::ErrorCode::kSourceTypeUnsupported,
        absl::StatusCode::kUnimplemented,
        "a .srt resource is a SubRip subtitle file and cannot be primary "
        "media; load it as a text track instead",
        {{"scheme", scheme}, {"extension", "srt"}}, *redactor_);
  }
  resolved.kind = entry->kind;
  resolved.byte_stream = entry->byte_stream;
  resolved.elementary_stream = entry->elementary_stream;
  resolved.url = *std::move(parsed);
  return resolved;
}

}
