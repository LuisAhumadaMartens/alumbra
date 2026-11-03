#ifndef UMBRA_SOURCE_HTTP_BYTE_STREAM_PROVIDER_H_
#define UMBRA_SOURCE_HTTP_BYTE_STREAM_PROVIDER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"
#include "umbra/source/credentials.h"

namespace umbra::source {

inline constexpr std::string_view kDefaultUserAgent = "Alumbra/0.1.0";
inline constexpr int kMaxRedirectLimit = 20;

struct HttpByteStreamOptions {
  absl::Duration connect_timeout = absl::Milliseconds(10000);
  absl::Duration read_timeout = absl::Milliseconds(20000);
  int max_redirects = kMaxRedirectLimit;
  bool allow_https_to_http_redirect = false;
  std::string user_agent;
  std::vector<penumbra::HttpHeader> headers;
};

[[nodiscard]] HttpByteStreamOptions HttpByteStreamOptionsFromSettings(
    const settings::Snapshot& snapshot);

struct HttpRequestReport {
  std::string url;
  penumbra::RequestClass request_class = penumbra::RequestClass::kMedia;
  int status = 0;
  penumbra::HttpProtocol protocol = penumbra::HttpProtocol::kUnknown;
  uint64_t body_bytes = 0;
  absl::Duration time_to_first_byte;
  absl::Duration transfer_duration;
  absl::Duration paused_duration;
  bool coarse_timing = false;
};

class HttpRequestObserver {
 public:
  virtual ~HttpRequestObserver() = default;
  virtual void OnRequestComplete(const HttpRequestReport& report) = 0;
};

struct HttpByteStreamDependencies {
  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<const base::UrlRedactor> redactor;
  std::shared_ptr<CredentialProvider> credentials;
  std::shared_ptr<HttpRequestObserver> observer;
  const base::TickClock* clock = nullptr;
};

namespace internal {

struct HttpProviderContext {
  HttpByteStreamDependencies dependencies;
  HttpByteStreamOptions options;
  std::map<std::string, penumbra::Credential> credentials_by_origin;
};

}

class HttpByteStreamProvider final : public penumbra::ByteStreamProvider {
 public:
  static absl::StatusOr<std::shared_ptr<HttpByteStreamProvider>> Create(
      HttpByteStreamDependencies dependencies, HttpByteStreamOptions options);

  [[nodiscard]] std::vector<std::string> Schemes() const override;
  absl::StatusOr<std::unique_ptr<penumbra::ByteStream>> Open(
      penumbra::ByteStreamRequest request,
      std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) override;

 private:
  explicit HttpByteStreamProvider(
      std::shared_ptr<internal::HttpProviderContext> context)
      : context_(std::move(context)) {}

  std::shared_ptr<internal::HttpProviderContext> context_;
};

}

#endif
