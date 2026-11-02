#ifndef UMBRA_SOURCE_SOURCE_RESOLVER_H_
#define UMBRA_SOURCE_SOURCE_RESOLVER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/credentials.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_type.h"
#include "umbra/source/url.h"

namespace umbra::source {

enum class UserInfoPolicy { kConvertToCredential, kReject };

struct ResolveOptions {
  std::optional<SourceType> type_hint;
  std::string platform = "desktop";
  UserInfoPolicy user_info = UserInfoPolicy::kConvertToCredential;
};

struct ResolvedSource {
  UrlReference url;
  ProviderKind kind = ProviderKind::kByteStream;
  std::shared_ptr<penumbra::ByteStreamProvider> byte_stream;
  std::shared_ptr<penumbra::ElementaryStreamProvider> elementary_stream;
  std::optional<SourceCredential> credential;
  std::optional<SourceType> explicit_type;
};

[[nodiscard]] absl::StatusOr<std::optional<SourceType>> ParseTypeHint(
    std::string_view setting);

class SourceResolver {
 public:
  SourceResolver(std::shared_ptr<const SchemeRegistry> registry,
                 std::shared_ptr<const base::UrlRedactor> redactor);

  [[nodiscard]] absl::StatusOr<ResolvedSource> Resolve(
      std::string_view url, const ResolveOptions& options = {}) const;

 private:
  std::shared_ptr<const SchemeRegistry> registry_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
};

}

#endif
