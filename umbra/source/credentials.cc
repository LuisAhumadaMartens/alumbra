#include "umbra/source/credentials.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source {
namespace {

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}

std::optional<std::string> PercentDecode(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') {
      decoded += text[i];
      continue;
    }
    if (i + 2 >= text.size()) {
      return std::nullopt;
    }
    const int high = HexValue(text[i + 1]);
    const int low = HexValue(text[i + 2]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    decoded += static_cast<char>((high * 16) + low);
    i += 2;
  }
  return decoded;
}

absl::StatusOr<penumbra::Credential> CredentialFromUserInfo(
    std::string_view userinfo) {
  const size_t colon = userinfo.find(':');
  const std::string_view user = userinfo.substr(0, colon);
  const std::string_view password = colon == std::string_view::npos
                                        ? std::string_view()
                                        : userinfo.substr(colon + 1);
  std::optional<std::string> decoded_user = PercentDecode(user);
  std::optional<std::string> decoded_password = PercentDecode(password);
  if (!decoded_user.has_value() || !decoded_password.has_value()) {
    return absl::InvalidArgumentError(
        "malformed percent escape in user information");
  }
  return penumbra::Credential{.username = *std::move(decoded_user),
                              .password = *std::move(decoded_password)};
}

}
