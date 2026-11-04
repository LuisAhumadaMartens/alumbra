#ifndef PENUMBRA_WEB_FETCH_HTTP_TRANSPORT_H_
#define PENUMBRA_WEB_FETCH_HTTP_TRANSPORT_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"

namespace penumbra::web {

enum class FetchCredentials { kOmit, kSameOrigin, kInclude };

struct FetchTransportOptions {
  FetchCredentials credentials = FetchCredentials::kSameOrigin;
  bool use_priority_hints = true;
};

class FetchHttpTransport final : public HttpTransport {
 public:
  class Core;

  static absl::StatusOr<std::shared_ptr<FetchHttpTransport>> Create(
      FetchTransportOptions options = {});

  FetchHttpTransport(const FetchHttpTransport&) = delete;
  FetchHttpTransport& operator=(const FetchHttpTransport&) = delete;
  ~FetchHttpTransport() override;

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request,
      std::shared_ptr<HttpTransportCallbacks> callbacks) override;
  absl::Status Stop() override;

 private:
  explicit FetchHttpTransport(std::shared_ptr<Core> core);

  std::shared_ptr<Core> core_;
};

}

#endif
