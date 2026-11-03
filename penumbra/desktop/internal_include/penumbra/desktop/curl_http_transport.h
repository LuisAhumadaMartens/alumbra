#ifndef PENUMBRA_DESKTOP_CURL_HTTP_TRANSPORT_H_
#define PENUMBRA_DESKTOP_CURL_HTTP_TRANSPORT_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"

namespace penumbra::desktop {

struct CurlTransportOptions {
  std::optional<std::string> ca_bundle_path = std::nullopt;
};

class CurlHttpTransport final : public HttpTransport {
 public:
  static absl::StatusOr<std::shared_ptr<CurlHttpTransport>> Create(
      CurlTransportOptions options = {});

  CurlHttpTransport(const CurlHttpTransport&) = delete;
  CurlHttpTransport& operator=(const CurlHttpTransport&) = delete;
  ~CurlHttpTransport() override;

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request,
      std::shared_ptr<HttpTransportCallbacks> callbacks) override;
  absl::Status Stop() override;

 private:
  class Core;

  explicit CurlHttpTransport(std::shared_ptr<Core> core);

  std::shared_ptr<Core> core_;
};

}

#endif
