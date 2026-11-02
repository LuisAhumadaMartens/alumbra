#ifndef PENUMBRA_INTERFACE_HTTP_TRANSPORT_H_
#define PENUMBRA_INTERFACE_HTTP_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/shared_bytes.h"

namespace penumbra {

enum class RequestClass { kManifest, kLicence, kInit, kMedia, kPrefetch };
enum class HttpMethod { kGet, kHead, kPost };
enum class RequestPriority { kLow, kNormal, kHigh };
enum class HttpProtocol { kUnknown, kHttp1_0, kHttp1_1, kHttp2, kHttp3 };

struct HttpHeader {
  std::string name;
  std::string value;
};

struct ByteRange {
  uint64_t offset = 0;
  std::optional<uint64_t> length;
};

struct RedirectPolicy {
  int max_redirects = 10;
  bool allow_https_downgrade = false;
};

struct HttpRequest {
  HttpMethod method = HttpMethod::kGet;
  std::string url;
  std::vector<HttpHeader> headers;
  std::optional<ByteRange> range;
  SharedBytes body;
  RequestClass request_class = RequestClass::kMedia;
  RequestPriority priority = RequestPriority::kNormal;
  bool include_credentials = false;
  absl::Duration connect_timeout = absl::Seconds(10);
  RedirectPolicy redirects;
};

struct HttpResponseHead {
  int status = 0;
  std::vector<HttpHeader> headers;
  HttpProtocol protocol = HttpProtocol::kUnknown;
  std::string final_url;
};

struct HttpChunk {
  SharedBytes data;
  absl::Duration arrival_time;
};

struct HttpMetrics {
  int status = 0;
  HttpProtocol protocol = HttpProtocol::kUnknown;
  std::string final_url;
  absl::Duration request_start;
  absl::Duration response_start;
  absl::Duration completion;
  uint64_t body_bytes = 0;
  std::optional<absl::Duration> dns;
  std::optional<absl::Duration> connect;
  std::optional<absl::Duration> tls;
  bool coarse_timing = false;
};

class HttpTransportCallbacks {
 public:
  virtual ~HttpTransportCallbacks() = default;
  virtual absl::Status OnResponse(HttpResponseHead head) = 0;
  virtual absl::Status OnChunk(HttpChunk chunk) = 0;
  virtual absl::Status OnComplete(HttpMetrics metrics) = 0;
  virtual absl::Status OnError(absl::Status error) = 0;
};

class HttpRequestHandle {
 public:
  virtual ~HttpRequestHandle() = default;
  virtual void Cancel() = 0;
  virtual absl::Status Pause() = 0;
  virtual absl::Status Resume() = 0;
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  virtual absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request,
      std::shared_ptr<HttpTransportCallbacks> callbacks) = 0;
  virtual absl::Status Stop() = 0;
};

class HttpTransportProvider {
 public:
  virtual ~HttpTransportProvider() = default;
  virtual absl::StatusOr<std::shared_ptr<HttpTransport>> GetHttpTransport() = 0;
};

}

#endif
