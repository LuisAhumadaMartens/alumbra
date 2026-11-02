#ifndef TEST_PENUMBRA_INTERFACE_FAKE_NETWORK_H_
#define TEST_PENUMBRA_INTERFACE_FAKE_NETWORK_H_

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/stream_provider.h"

namespace penumbra::test {

class ScriptedDelivery {
 public:
  using Event = absl::AnyInvocable<absl::Status() &&>;

  void Enqueue(Event event);
  bool Step();
  void Cancel();
  absl::Status Pause();
  absl::Status Resume();

  [[nodiscard]] bool Finished() const;
  [[nodiscard]] bool Cancelled() const;
  [[nodiscard]] bool Paused() const;
  [[nodiscard]] bool InFlight() const;
  [[nodiscard]] size_t Delivered() const;
  [[nodiscard]] absl::Status DeliveryStatus() const;

 private:
  mutable std::mutex mutex_;
  std::deque<Event> events_;
  bool paused_ = false;
  bool cancelled_ = false;
  size_t in_flight_ = 0;
  size_t delivered_ = 0;
  absl::Status delivery_status_;
};

struct HttpScript {
  int status = 200;
  std::vector<HttpHeader> headers;
  std::vector<HttpChunk> chunks;
  HttpProtocol protocol = HttpProtocol::kHttp2;
  std::string final_url;
  absl::Duration request_start;
  absl::Duration response_start;
  absl::Duration completion;
  bool coarse_timing = false;
  std::optional<absl::Status> failure;
};

class FakeHttpHandle;

class FakeHttpTransport final : public HttpTransport {
 public:
  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request,
      std::shared_ptr<HttpTransportCallbacks> callbacks) override;
  absl::Status Stop() override;

  absl::Status SetScript(HttpScript script);
  size_t Pump(size_t max_events = static_cast<size_t>(-1));
  [[nodiscard]] std::vector<HttpRequest> Requests() const;

 private:
  mutable std::mutex mutex_;
  HttpScript script_;
  std::vector<HttpRequest> requests_;
  std::vector<std::shared_ptr<ScriptedDelivery>> deliveries_;
  bool stopped_ = false;
};

struct ByteStreamScript {
  ByteStreamInfo info;
  std::vector<ByteStreamChunk> chunks;
  std::optional<absl::Status> failure;
};

class FakeByteStreamProvider final : public ByteStreamProvider {
 public:
  explicit FakeByteStreamProvider(std::vector<std::string> schemes);
  [[nodiscard]] std::vector<std::string> Schemes() const override;
  absl::StatusOr<std::unique_ptr<ByteStream>> Open(
      ByteStreamRequest request,
      std::shared_ptr<ByteStreamCallbacks> callbacks) override;

  void SetScript(ByteStreamScript script);
  size_t Pump(size_t max_events = static_cast<size_t>(-1));
  [[nodiscard]] std::vector<ByteStreamRequest> Requests() const;

 private:
  std::vector<std::string> schemes_;
  mutable std::mutex mutex_;
  ByteStreamScript script_;
  std::vector<ByteStreamRequest> requests_;
  std::vector<std::shared_ptr<ScriptedDelivery>> deliveries_;
};

struct ElementaryStreamScript {
  std::vector<StreamInfo> streams;
  std::vector<EncodedSample> samples;
  std::optional<absl::Status> failure;
};

class FakeElementaryStreamProvider final : public ElementaryStreamProvider {
 public:
  explicit FakeElementaryStreamProvider(std::vector<std::string> schemes);
  [[nodiscard]] std::vector<std::string> Schemes() const override;
  absl::StatusOr<std::unique_ptr<ElementaryStream>> Open(
      ElementaryStreamRequest request,
      std::shared_ptr<ElementaryStreamCallbacks> callbacks) override;

  void SetScript(ElementaryStreamScript script);
  size_t Pump(size_t max_events = static_cast<size_t>(-1));
  [[nodiscard]] std::vector<ElementaryStreamRequest> Requests() const;

 private:
  std::vector<std::string> schemes_;
  mutable std::mutex mutex_;
  ElementaryStreamScript script_;
  std::vector<ElementaryStreamRequest> requests_;
  std::vector<std::shared_ptr<ScriptedDelivery>> deliveries_;
};

}

#endif
