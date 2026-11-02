#ifndef UMBRA_SOURCE_URL_H_
#define UMBRA_SOURCE_URL_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::source {

struct UrlReference {
  std::optional<std::string> scheme;
  std::optional<std::string> userinfo;
  std::optional<std::string> host;
  std::optional<std::string> port;
  std::string path;
  std::optional<std::string> query;
  std::optional<std::string> fragment;

  static absl::StatusOr<UrlReference> Parse(std::string_view text);
  static absl::StatusOr<UrlReference> ParseAbsolute(std::string_view text);

  [[nodiscard]] bool has_authority() const { return host.has_value(); }
  [[nodiscard]] bool is_absolute() const { return scheme.has_value(); }
  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] std::string Origin() const;
  [[nodiscard]] std::string PathExtension() const;
  [[nodiscard]] UrlReference WithoutUserInfo() const;

  friend bool operator==(const UrlReference&, const UrlReference&) = default;
};

[[nodiscard]] std::string RemoveDotSegments(std::string_view path);

[[nodiscard]] absl::StatusOr<UrlReference> ResolveReference(
    const UrlReference& base, const UrlReference& reference);

[[nodiscard]] absl::StatusOr<UrlReference> ResolveReference(
    std::string_view base, std::string_view reference);

}

#endif
