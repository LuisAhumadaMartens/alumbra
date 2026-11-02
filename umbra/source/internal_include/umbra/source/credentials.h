#ifndef UMBRA_SOURCE_CREDENTIALS_H_
#define UMBRA_SOURCE_CREDENTIALS_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source {

struct CredentialChallenge {
  std::string origin;
  std::string scheme;
  std::string realm;
};

struct SourceCredential {
  std::string origin;
  penumbra::Credential credential;
};

using CredentialResponse =
    absl::AnyInvocable<void(std::optional<penumbra::Credential>) &&>;

class CredentialProvider {
 public:
  virtual ~CredentialProvider() = default;
  virtual void RequestCredential(const CredentialChallenge& challenge,
                                 CredentialResponse respond) = 0;
};

[[nodiscard]] absl::StatusOr<penumbra::Credential> CredentialFromUserInfo(
    std::string_view userinfo);

[[nodiscard]] std::optional<std::string> PercentDecode(std::string_view text);

}

#endif
