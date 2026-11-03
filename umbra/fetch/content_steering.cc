#include "umbra/fetch/content_steering.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/task_runner.h"

namespace umbra::fetch {

absl::Status ValidateSteeringManifest(const SteeringManifest& manifest) {
  if (manifest.version != kSteeringManifestVersion) {
    return absl::InvalidArgumentError(absl::StrCat(
        "unsupported steering manifest version ", manifest.version));
  }
  if (manifest.ttl < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("steering TTL must not be negative");
  }
  std::set<std::string> seen;
  for (const std::string& pathway : manifest.pathway_priority) {
    if (pathway.empty() || !seen.insert(pathway).second) {
      return absl::InvalidArgumentError(
          "steering priority needs distinct nonempty pathway identifiers");
    }
  }
  return absl::OkStatus();
}

class SteeringClient::Impl : public std::enable_shared_from_this<Impl> {
 public:
  Impl(SteeringClientDependencies dependencies, SteeringClientConfig config)
      : deps_(std::move(dependencies)),
        config_(std::move(config)),
        uri_(config_.uri) {}

  void Start();
  void Stop();
  void Poll(uint64_t epoch);
  void HandleResult(uint64_t epoch, const absl::Status& status,
                    const penumbra::SharedBytes& body);

  [[nodiscard]] uint64_t PollCount() const { return poll_count_; }

 private:
  class Callbacks;

  void Schedule(absl::Duration delay);
  void Fail(const absl::Status& status);

  SteeringClientDependencies deps_;
  SteeringClientConfig config_;
  std::string uri_;
  uint64_t epoch_ = 0;
  uint64_t poll_count_ = 0;
  bool running_ = false;
  std::unique_ptr<penumbra::HttpRequestHandle> handle_;
  base::SequenceChecker sequence_;
};

class SteeringClient::Impl::Callbacks final
    : public penumbra::HttpTransportCallbacks {
 public:
  Callbacks(std::weak_ptr<Impl> impl,
            std::shared_ptr<base::SequencedTaskRunner> runner, uint64_t epoch,
            size_t max_body_bytes)
      : impl_(std::move(impl)),
        runner_(std::move(runner)),
        epoch_(epoch),
        max_body_bytes_(max_body_bytes) {}

  absl::Status OnResponse(penumbra::HttpResponseHead head) override {
    http_status_ = head.status;
    return absl::OkStatus();
  }

  absl::Status OnChunk(penumbra::HttpChunk chunk) override {
    const size_t size = chunk.data.size();
    if (body_.size() + size > max_body_bytes_) {
      overflow_ = true;
      return absl::OkStatus();
    }
    const std::span<const uint8_t> bytes = chunk.data.span();
    body_.insert(body_.end(), bytes.begin(), bytes.end());
    return absl::OkStatus();
  }

  absl::Status OnComplete(
      [[maybe_unused]] penumbra::HttpMetrics metrics) override {
    absl::Status status = absl::OkStatus();
    if (overflow_) {
      status = absl::ResourceExhaustedError("steering manifest is too large");
    } else if (http_status_ < 200 || http_status_ > 299) {
      status = absl::UnavailableError(
          absl::StrCat("steering manifest HTTP status ", http_status_));
    }
    Post(std::move(status), penumbra::SharedBytes::Adopt(std::move(body_)));
    return absl::OkStatus();
  }

  absl::Status OnError(absl::Status error) override {
    Post(std::move(error), penumbra::SharedBytes());
    return absl::OkStatus();
  }

 private:
  void Post(absl::Status status, penumbra::SharedBytes body) {
    runner_->PostTask([impl = impl_, epoch = epoch_, status = std::move(status),
                       body = std::move(body)]() mutable {
      if (const std::shared_ptr<Impl> target = impl.lock()) {
        target->HandleResult(epoch, status, body);
      }
    });
  }

  std::weak_ptr<Impl> impl_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  uint64_t epoch_;
  size_t max_body_bytes_;
  int http_status_ = 0;
  bool overflow_ = false;
  std::vector<uint8_t> body_;
};

void SteeringClient::Impl::Start() {
  assert(sequence_.CalledOnValidSequence());
  if (running_) {
    return;
  }
  running_ = true;
  ++epoch_;
  Poll(epoch_);
}

void SteeringClient::Impl::Stop() {
  assert(sequence_.CalledOnValidSequence());
  running_ = false;
  ++epoch_;
  if (handle_ != nullptr) {
    handle_->Cancel();
    handle_.reset();
  }
}

void SteeringClient::Impl::Schedule(absl::Duration delay) {
  const uint64_t epoch = epoch_;
  deps_.runner->PostDelayedTask(
      [weak = weak_from_this(), epoch] {
        if (const std::shared_ptr<Impl> target = weak.lock()) {
          target->Poll(epoch);
        }
      },
      delay);
}

void SteeringClient::Impl::Poll(uint64_t epoch) {
  assert(sequence_.CalledOnValidSequence());
  if (!running_ || epoch != epoch_) {
    return;
  }
  ++poll_count_;
  penumbra::HttpRequest request;
  request.url = uri_;
  request.request_class = penumbra::RequestClass::kManifest;
  request.priority = penumbra::RequestPriority::kHigh;
  request.connect_timeout = config_.connect_timeout;
  auto callbacks = std::make_shared<Callbacks>(weak_from_this(), deps_.runner,
                                               epoch_, config_.max_body_bytes);
  auto started = deps_.transport->Start(std::move(request), callbacks);
  if (!started.ok()) {
    Fail(started.status());
    return;
  }
  handle_ = *std::move(started);
}

void SteeringClient::Impl::Fail(const absl::Status& status) {
  handle_.reset();
  deps_.listener->OnSteeringFailure(status);
  Schedule(config_.failure_retry);
}

void SteeringClient::Impl::HandleResult(uint64_t epoch,
                                        const absl::Status& status,
                                        const penumbra::SharedBytes& body) {
  assert(sequence_.CalledOnValidSequence());
  if (!running_ || epoch != epoch_) {
    return;
  }
  handle_.reset();
  if (!status.ok()) {
    Fail(status);
    return;
  }
  absl::StatusOr<SteeringManifest> parsed = deps_.parser->Parse(body);
  if (parsed.ok()) {
    if (absl::Status valid = ValidateSteeringManifest(*parsed); !valid.ok()) {
      parsed = std::move(valid);
    }
  }
  if (!parsed.ok()) {
    Fail(parsed.status());
    return;
  }
  if (!parsed->reload_uri.empty()) {
    uri_ = parsed->reload_uri;
  }
  const absl::Duration ttl = std::max(parsed->ttl, config_.min_ttl);
  deps_.listener->OnSteeringManifest(*parsed);
  Schedule(ttl);
}

absl::StatusOr<std::unique_ptr<SteeringClient>> SteeringClient::Create(
    SteeringClientDependencies dependencies, SteeringClientConfig config) {
  if (dependencies.transport == nullptr || dependencies.runner == nullptr ||
      dependencies.parser == nullptr || dependencies.listener == nullptr) {
    return absl::InvalidArgumentError(
        "transport, runner, parser and listener are required");
  }
  if (config.uri.empty()) {
    return absl::InvalidArgumentError("a steering URI is required");
  }
  if (config.min_ttl <= absl::ZeroDuration() ||
      config.failure_retry <= absl::ZeroDuration() ||
      config.max_body_bytes == 0) {
    return absl::InvalidArgumentError(
        "steering timings and body limit must be positive");
  }
  auto impl =
      std::make_shared<Impl>(std::move(dependencies), std::move(config));
  return std::unique_ptr<SteeringClient>(new SteeringClient(std::move(impl)));
}

SteeringClient::SteeringClient(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SteeringClient::~SteeringClient() { impl_->Stop(); }

void SteeringClient::Start() { impl_->Start(); }

void SteeringClient::Stop() { impl_->Stop(); }

uint64_t SteeringClient::PollCount() const { return impl_->PollCount(); }

}
