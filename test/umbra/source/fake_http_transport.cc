#include "fake_http_transport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"

namespace umbra::source::test {
namespace {

class FakeHttpHandle final : public penumbra::HttpRequestHandle {
 public:
  explicit FakeHttpHandle(std::shared_ptr<FakeHttpRequestState> state)
      : state_(std::move(state)) {}
  FakeHttpHandle(const FakeHttpHandle&) = delete;
  FakeHttpHandle& operator=(const FakeHttpHandle&) = delete;
  ~FakeHttpHandle() override { state_->cancelled = true; }

  void Cancel() override { state_->cancelled = true; }

  absl::Status Pause() override {
    if (state_->cancelled) {
      return absl::FailedPreconditionError("cancelled");
    }
    state_->paused = true;
    ++state_->pauses;
    return absl::OkStatus();
  }

  absl::Status Resume() override {
    if (state_->cancelled) {
      return absl::FailedPreconditionError("cancelled");
    }
    state_->paused = false;
    ++state_->resumes;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<FakeHttpRequestState> state_;
};

}

absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>>
FakeHttpTransport::Start(
    penumbra::HttpRequest request,
    std::shared_ptr<penumbra::HttpTransportCallbacks> callbacks) {
  if (start_failure_.has_value()) {
    return std::exchange(start_failure_, std::nullopt).value();
  }
  auto state = std::make_shared<FakeHttpRequestState>();
  state->request = std::move(request);
  state->callbacks = std::move(callbacks);
  requests_.push_back(state);
  return std::make_unique<FakeHttpHandle>(std::move(state));
}

absl::Status FakeHttpTransport::Stop() { return absl::OkStatus(); }

void FakeHttpTransport::FailNextStart(absl::Status status) {
  start_failure_ = std::move(status);
}

void FakeHttpTransport::Respond(size_t index, int status,
                                std::vector<penumbra::HttpHeader> headers,
                                penumbra::HttpProtocol protocol) {
  penumbra::HttpResponseHead head;
  head.status = status;
  head.headers = std::move(headers);
  head.protocol = protocol;
  head.final_url = requests_[index]->request.url;
  static_cast<void>(requests_[index]->callbacks->OnResponse(std::move(head)));
}

void FakeHttpTransport::Chunk(size_t index, const std::vector<uint8_t>& bytes,
                              absl::Duration arrival) {
  penumbra::HttpChunk chunk;
  chunk.data = penumbra::SharedBytes::Copy(bytes);
  chunk.arrival_time = arrival;
  static_cast<void>(requests_[index]->callbacks->OnChunk(std::move(chunk)));
}

void FakeHttpTransport::Complete(size_t index, int status, uint64_t body_bytes,
                                 bool coarse_timing) {
  penumbra::HttpMetrics metrics;
  metrics.status = status;
  metrics.protocol = penumbra::HttpProtocol::kHttp2;
  metrics.final_url = requests_[index]->request.url;
  metrics.request_start = absl::Milliseconds(100);
  metrics.response_start = absl::Milliseconds(150);
  metrics.completion = absl::Milliseconds(450);
  metrics.body_bytes = body_bytes;
  metrics.coarse_timing = coarse_timing;
  static_cast<void>(
      requests_[index]->callbacks->OnComplete(std::move(metrics)));
}

void FakeHttpTransport::Fail(size_t index, absl::Status status) {
  static_cast<void>(requests_[index]->callbacks->OnError(std::move(status)));
}

}
