#ifndef TEST_UMBRA_SOURCE_FAKE_HTTP_TRANSPORT_H_
#define TEST_UMBRA_SOURCE_FAKE_HTTP_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"

namespace umbra::source::test {

struct FakeHttpRequestState {
  penumbra::HttpRequest request;
  std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks;
  bool cancelled = false;
  bool paused = false;
  int pauses = 0;
  int resumes = 0;
};

class FakeHttpTransport final : public penumbra::HttpTransport {
 public:
  absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> Start(
      penumbra::HttpRequest request,
      std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) override;
  absl::Status Stop() override;

  void FailNextStart(absl::Status status);

  void Respond(
      size_t index, int status, std::vector<penumbra::HttpHeader> headers,
      penumbra::HttpProtocol protocol = penumbra::HttpProtocol::kHttp2);
  void Chunk(size_t index, const std::vector<uint8_t>& bytes,
             absl::Duration arrival = absl::ZeroDuration());
  void Complete(size_t index, int status, uint64_t body_bytes,
                bool coarse_timing = false);
  void Fail(size_t index, absl::Status status);

  [[nodiscard]] const std::vector<std::shared_ptr<FakeHttpRequestState>>&
  Requests() const {
    return requests_;
  }

 private:
  std::vector<std::shared_ptr<FakeHttpRequestState>> requests_;
  std::optional<absl::Status> start_failure_;
};

}

#endif
