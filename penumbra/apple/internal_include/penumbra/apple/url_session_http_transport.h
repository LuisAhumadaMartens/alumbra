#ifndef PENUMBRA_APPLE_URL_SESSION_HTTP_TRANSPORT_H_
#define PENUMBRA_APPLE_URL_SESSION_HTTP_TRANSPORT_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"

namespace penumbra::apple {

struct UrlSessionTransportOptions {
  bool allows_constrained_network_access = true;
  bool allows_expensive_network_access = true;
  bool assume_http3_capable = false;
  absl::Duration read_timeout = absl::Seconds(20);
};

class UrlSessionHttpTransport final : public HttpTransport {
 public:
  class Core;

  static absl::StatusOr<std::shared_ptr<UrlSessionHttpTransport>> Create(
      UrlSessionTransportOptions options = {});

  UrlSessionHttpTransport(const UrlSessionHttpTransport&) = delete;
  UrlSessionHttpTransport& operator=(const UrlSessionHttpTransport&) = delete;
  ~UrlSessionHttpTransport() override;

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request,
      std::shared_ptr<HttpTransportCallbacks> callbacks) override;
  absl::Status Stop() override;

 private:
  explicit UrlSessionHttpTransport(std::shared_ptr<Core> core);

  std::shared_ptr<Core> core_;
};

}

#endif
