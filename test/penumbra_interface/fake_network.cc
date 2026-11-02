#include "fake_network.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/stream_provider.h"

namespace penumbra::test {
namespace {

template <typename Base>
class ScriptedHandle final : public Base {
 public:
  explicit ScriptedHandle(std::shared_ptr<ScriptedDelivery> delivery)
      : delivery_(std::move(delivery)) {}
  ScriptedHandle(const ScriptedHandle&) = delete;
  ScriptedHandle& operator=(const ScriptedHandle&) = delete;
  ~ScriptedHandle() override { delivery_->Cancel(); }

  void Cancel() override {
    PortCallScope const scope;
    delivery_->Cancel();
  }
  absl::Status Pause() override {
    PortCallScope const scope;
    return delivery_->Pause();
  }
  absl::Status Resume() override {
    PortCallScope const scope;
    return delivery_->Resume();
  }

 private:
  std::shared_ptr<ScriptedDelivery> delivery_;
};

size_t PumpAll(std::mutex& mutex,
               const std::vector<std::shared_ptr<ScriptedDelivery>>& all,
               size_t max_events) {
  std::vector<std::shared_ptr<ScriptedDelivery>> snapshot;
  {
    std::scoped_lock const lock(mutex);
    snapshot = all;
  }
  size_t delivered = 0;
  bool progress = true;
  while (progress && delivered < max_events) {
    progress = false;
    for (const auto& delivery : snapshot) {
      if (delivered < max_events && delivery->Step()) {
        ++delivered;
        progress = true;
      }
    }
  }
  return delivered;
}

bool SchemeMatches(const std::vector<std::string>& schemes,
                   std::string_view url) {
  const size_t colon = url.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }
  std::string scheme(url.substr(0, colon));
  std::ranges::transform(scheme, scheme.begin(), [](char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return std::ranges::find(schemes, scheme) != schemes.end();
}

}

void ScriptedDelivery::Enqueue(Event event) {
  std::scoped_lock const lock(mutex_);
  events_.push_back(std::move(event));
}

bool ScriptedDelivery::Step() {
  Event event;
  {
    std::scoped_lock const lock(mutex_);
    if (cancelled_ || paused_ || events_.empty()) {
      return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    ++in_flight_;
  }
  absl::Status status;
  {
    CallbackScope const scope;
    status = std::move(event)();
  }
  std::scoped_lock const lock(mutex_);
  --in_flight_;
  ++delivered_;
  if (!status.ok()) {
    delivery_status_ = std::move(status);
  }
  return true;
}

void ScriptedDelivery::Cancel() {
  std::scoped_lock const lock(mutex_);
  cancelled_ = true;
}

absl::Status ScriptedDelivery::Pause() {
  std::scoped_lock const lock(mutex_);
  if (cancelled_ || events_.empty()) {
    return absl::FailedPreconditionError("request is not active");
  }
  paused_ = true;
  return absl::OkStatus();
}

absl::Status ScriptedDelivery::Resume() {
  std::scoped_lock const lock(mutex_);
  if (cancelled_ || events_.empty()) {
    return absl::FailedPreconditionError("request is not active");
  }
  paused_ = false;
  return absl::OkStatus();
}

bool ScriptedDelivery::Finished() const {
  std::scoped_lock const lock(mutex_);
  return events_.empty();
}

bool ScriptedDelivery::Cancelled() const {
  std::scoped_lock const lock(mutex_);
  return cancelled_;
}

bool ScriptedDelivery::Paused() const {
  std::scoped_lock const lock(mutex_);
  return paused_;
}

bool ScriptedDelivery::InFlight() const {
  std::scoped_lock const lock(mutex_);
  return in_flight_ != 0;
}

size_t ScriptedDelivery::Delivered() const {
  std::scoped_lock const lock(mutex_);
  return delivered_;
}

absl::Status ScriptedDelivery::DeliveryStatus() const {
  std::scoped_lock const lock(mutex_);
  return delivery_status_;
}

absl::Status FakeHttpTransport::SetScript(HttpScript script) {
  if (script.status != 0 && (script.status < 100 || script.status > 599)) {
    return absl::InvalidArgumentError("invalid HTTP status");
  }
  absl::Duration previous = script.response_start;
  for (const auto& chunk : script.chunks) {
    if (chunk.data.empty() || chunk.arrival_time < previous) {
      return absl::InvalidArgumentError(
          "chunks must be nonempty with monotonic arrival times");
    }
    previous = chunk.arrival_time;
  }
  if (script.completion < previous ||
      script.response_start < script.request_start) {
    return absl::InvalidArgumentError("metrics times must be monotonic");
  }
  std::scoped_lock const lock(mutex_);
  script_ = std::move(script);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<HttpRequestHandle>> FakeHttpTransport::Start(
    HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
  PortCallScope const scope;
  if (callbacks == nullptr || request.url.empty() ||
      request.connect_timeout <= absl::ZeroDuration() ||
      request.redirects.max_redirects < 0 ||
      (request.method != HttpMethod::kPost && !request.body.empty())) {
    return absl::InvalidArgumentError("invalid HTTP request");
  }
  HttpScript script;
  {
    std::scoped_lock const lock(mutex_);
    if (stopped_) {
      return absl::FailedPreconditionError("transport is stopped");
    }
    script = script_;
    requests_.push_back(request);
  }
  if (script.final_url.empty()) {
    script.final_url = request.url;
  }
  auto delivery = std::make_shared<ScriptedDelivery>();
  if (script.status != 0) {
    HttpResponseHead head{.status = script.status,
                          .headers = script.headers,
                          .protocol = script.protocol,
                          .final_url = script.final_url};
    delivery->Enqueue([callbacks, head = std::move(head)]() mutable {
      return callbacks->OnResponse(std::move(head));
    });
  }
  uint64_t body_bytes = 0;
  for (const auto& chunk : script.chunks) {
    body_bytes += chunk.data.size();
    delivery->Enqueue([callbacks, chunk = chunk]() mutable {
      return callbacks->OnChunk(std::move(chunk));
    });
  }
  if (script.failure.has_value()) {
    delivery->Enqueue([callbacks, failure = *script.failure]() mutable {
      return callbacks->OnError(std::move(failure));
    });
  } else {
    HttpMetrics metrics;
    metrics.status = script.status;
    metrics.protocol = script.protocol;
    metrics.final_url = script.final_url;
    metrics.request_start = script.request_start;
    metrics.response_start = script.response_start;
    metrics.completion = script.completion;
    metrics.body_bytes = body_bytes;
    metrics.coarse_timing = script.coarse_timing;
    delivery->Enqueue([callbacks, metrics = std::move(metrics)]() mutable {
      return callbacks->OnComplete(std::move(metrics));
    });
  }
  {
    std::scoped_lock const lock(mutex_);
    deliveries_.push_back(delivery);
  }
  return std::make_unique<ScriptedHandle<HttpRequestHandle>>(
      std::move(delivery));
}

absl::Status FakeHttpTransport::Stop() {
  PortCallScope const scope;
  std::scoped_lock const lock(mutex_);
  stopped_ = true;
  bool busy = false;
  for (const auto& delivery : deliveries_) {
    delivery->Cancel();
    busy = busy || delivery->InFlight();
  }
  if (busy) {
    return absl::UnavailableError("a transport callback is still in flight");
  }
  return absl::OkStatus();
}

size_t FakeHttpTransport::Pump(size_t max_events) {
  return PumpAll(mutex_, deliveries_, max_events);
}

std::vector<HttpRequest> FakeHttpTransport::Requests() const {
  std::scoped_lock const lock(mutex_);
  return requests_;
}

FakeByteStreamProvider::FakeByteStreamProvider(std::vector<std::string> schemes)
    : schemes_(std::move(schemes)) {}

std::vector<std::string> FakeByteStreamProvider::Schemes() const {
  return schemes_;
}

void FakeByteStreamProvider::SetScript(ByteStreamScript script) {
  std::scoped_lock const lock(mutex_);
  script_ = std::move(script);
}

absl::StatusOr<std::unique_ptr<ByteStream>> FakeByteStreamProvider::Open(
    ByteStreamRequest request, std::shared_ptr<ByteStreamCallbacks> callbacks) {
  PortCallScope const scope;
  if (callbacks == nullptr || !SchemeMatches(schemes_, request.url)) {
    return absl::InvalidArgumentError("invalid byte stream request");
  }
  ByteStreamScript script;
  {
    std::scoped_lock const lock(mutex_);
    script = script_;
    if (request.offset > 0 && !script.info.seekable) {
      return absl::InvalidArgumentError("the stream is not seekable");
    }
    requests_.push_back(request);
  }
  auto delivery = std::make_shared<ScriptedDelivery>();
  delivery->Enqueue([callbacks, info = script.info]() mutable {
    return callbacks->OnOpen(std::move(info));
  });
  for (const auto& chunk : script.chunks) {
    delivery->Enqueue([callbacks, chunk = chunk]() mutable {
      return callbacks->OnData(std::move(chunk));
    });
  }
  if (script.failure.has_value()) {
    delivery->Enqueue([callbacks, failure = *script.failure]() mutable {
      return callbacks->OnError(std::move(failure));
    });
  } else {
    delivery->Enqueue([callbacks]() { return callbacks->OnEnd(); });
  }
  {
    std::scoped_lock const lock(mutex_);
    deliveries_.push_back(delivery);
  }
  return std::make_unique<ScriptedHandle<ByteStream>>(std::move(delivery));
}

size_t FakeByteStreamProvider::Pump(size_t max_events) {
  return PumpAll(mutex_, deliveries_, max_events);
}

std::vector<ByteStreamRequest> FakeByteStreamProvider::Requests() const {
  std::scoped_lock const lock(mutex_);
  return requests_;
}

FakeElementaryStreamProvider::FakeElementaryStreamProvider(
    std::vector<std::string> schemes)
    : schemes_(std::move(schemes)) {}

std::vector<std::string> FakeElementaryStreamProvider::Schemes() const {
  return schemes_;
}

void FakeElementaryStreamProvider::SetScript(ElementaryStreamScript script) {
  std::scoped_lock const lock(mutex_);
  script_ = std::move(script);
}

absl::StatusOr<std::unique_ptr<ElementaryStream>>
FakeElementaryStreamProvider::Open(
    ElementaryStreamRequest request,
    std::shared_ptr<ElementaryStreamCallbacks> callbacks) {
  PortCallScope const scope;
  if (callbacks == nullptr || !SchemeMatches(schemes_, request.url)) {
    return absl::InvalidArgumentError("invalid elementary stream request");
  }
  ElementaryStreamScript script;
  {
    std::scoped_lock const lock(mutex_);
    script = script_;
    requests_.push_back(request);
  }
  auto delivery = std::make_shared<ScriptedDelivery>();
  delivery->Enqueue([callbacks, streams = script.streams]() mutable {
    return callbacks->OnStreams(std::move(streams));
  });
  int64_t index = 0;
  for (const auto& sample : script.samples) {
    const auto shared = std::make_shared<const EncodedSample>(sample);
    const absl::Duration arrival = absl::Milliseconds(index++);
    delivery->Enqueue([callbacks, shared, arrival]() {
      return callbacks->OnSample(*shared, arrival);
    });
  }
  if (script.failure.has_value()) {
    delivery->Enqueue([callbacks, failure = *script.failure]() mutable {
      return callbacks->OnError(std::move(failure));
    });
  } else {
    delivery->Enqueue([callbacks]() { return callbacks->OnEnd(); });
  }
  {
    std::scoped_lock const lock(mutex_);
    deliveries_.push_back(delivery);
  }
  return std::make_unique<ScriptedHandle<ElementaryStream>>(
      std::move(delivery));
}

size_t FakeElementaryStreamProvider::Pump(size_t max_events) {
  return PumpAll(mutex_, deliveries_, max_events);
}

std::vector<ElementaryStreamRequest> FakeElementaryStreamProvider::Requests()
    const {
  std::scoped_lock const lock(mutex_);
  return requests_;
}

}
