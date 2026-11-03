#include "umbra/ads/beacon_dispatcher.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/base/task_runner.h"

namespace umbra::ads {
namespace {

struct BeaconOperation {
  std::atomic<bool> completed = false;
  std::unique_ptr<penumbra::HttpRequestHandle> handle;
  BeaconDispatcher::DebugLogger logger;

  void Finish(bool success, int status) {
    if (!completed.exchange(true)) {
      if (logger) {
        logger(success, status);
      }
      if (handle) {
        handle->Cancel();
      }
    }
  }
};

class BeaconCallbacks final : public penumbra::HttpTransportCallbacks {
 public:
  BeaconCallbacks(std::shared_ptr<BeaconOperation> operation,
                  std::shared_ptr<base::SequencedTaskRunner> runner)
      : operation_(std::move(operation)), runner_(std::move(runner)) {}

  absl::Status OnResponse(penumbra::HttpResponseHead head) override {
    const int status = head.status;
    runner_->PostTask([operation = operation_, status]() {
      if (status < 200 || status >= 300) {
        operation->Finish(false, status);
      }
    });
    return absl::OkStatus();
  }

  absl::Status OnChunk(penumbra::HttpChunk chunk) override {
    static_cast<void>(chunk);
    return absl::OkStatus();
  }

  absl::Status OnComplete(penumbra::HttpMetrics metrics) override {
    runner_->PostTask([operation = operation_, status = metrics.status]() {
      operation->Finish(status >= 200 && status < 300, status);
    });
    return absl::OkStatus();
  }

  absl::Status OnError(absl::Status error) override {
    static_cast<void>(error);
    runner_->PostTask(
        [operation = operation_]() { operation->Finish(false, 0); });
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<BeaconOperation> operation_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
};

}

struct BeaconDispatcher::State {
  State(std::shared_ptr<penumbra::HttpTransport> transport_value,
        std::shared_ptr<base::SequencedTaskRunner> runner_value,
        absl::Duration timeout_value, DebugLogger logger_value)
      : transport(std::move(transport_value)),
        runner(std::move(runner_value)),
        timeout(timeout_value),
        logger(std::move(logger_value)) {}

  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  absl::Duration timeout;
  DebugLogger logger;
  std::atomic<bool> stopped = false;
};

BeaconDispatcher::BeaconDispatcher(
    std::shared_ptr<penumbra::HttpTransport> transport,
    std::shared_ptr<base::SequencedTaskRunner> network_runner,
    absl::Duration timeout, DebugLogger logger)
    : state_(std::make_shared<State>(std::move(transport),
                                     std::move(network_runner), timeout,
                                     std::move(logger))) {}

BeaconDispatcher::~BeaconDispatcher() { state_->stopped = true; }

void BeaconDispatcher::Dispatch(std::string url) {
  const std::shared_ptr<State> state = state_;
  state->runner->PostTask([state, url = std::move(url)]() mutable {
    if (state->stopped || !state->transport) {
      return;
    }
    auto operation = std::make_shared<BeaconOperation>();
    operation->logger = state->logger;
    penumbra::HttpRequest request;
    request.method = penumbra::HttpMethod::kGet;
    request.url = std::move(url);
    request.request_class = penumbra::RequestClass::kPrefetch;
    request.priority = penumbra::RequestPriority::kLow;
    request.connect_timeout = state->timeout;
    auto started = state->transport->Start(
        std::move(request),
        std::make_shared<BeaconCallbacks>(operation, state->runner));
    if (!started.ok()) {
      operation->Finish(false, 0);
      return;
    }
    operation->handle = std::move(*started);
    state->runner->PostDelayedTask(
        [operation]() { operation->Finish(false, 0); }, state->timeout);
  });
}

}
