#include "fake_stream_provider.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"

namespace umbra::source::test {
namespace {

class FakeStreamHandle final : public penumbra::ByteStream {
 public:
  explicit FakeStreamHandle(std::shared_ptr<FakeStreamState> state)
      : state_(std::move(state)) {}
  FakeStreamHandle(const FakeStreamHandle&) = delete;
  FakeStreamHandle& operator=(const FakeStreamHandle&) = delete;
  ~FakeStreamHandle() override { state_->cancelled = true; }

  void Cancel() override { state_->cancelled = true; }

  absl::Status Pause() override {
    if (state_->cancelled || state_->ended) {
      return absl::FailedPreconditionError("not active");
    }
    state_->paused = true;
    ++state_->pauses;
    return absl::OkStatus();
  }

  absl::Status Resume() override {
    if (state_->cancelled || state_->ended) {
      return absl::FailedPreconditionError("not active");
    }
    state_->paused = false;
    ++state_->resumes;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<FakeStreamState> state_;
};

}

FakeStreamProvider::FakeStreamProvider(std::vector<uint8_t> content,
                                       bool seekable, bool size_known,
                                       size_t chunk_bytes)
    : content_(std::move(content)),
      seekable_(seekable),
      size_known_(size_known),
      chunk_bytes_(chunk_bytes) {}

std::vector<std::string> FakeStreamProvider::Schemes() const {
  return {"fake"};
}

absl::StatusOr<std::unique_ptr<penumbra::ByteStream>> FakeStreamProvider::Open(
    penumbra::ByteStreamRequest request,
    std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) {
  if (reject_.has_value()) {
    return *reject_;
  }
  auto state = std::make_shared<FakeStreamState>();
  state->request = request;
  state->callbacks = std::move(callbacks);
  state->next = request.offset;
  streams_.push_back(state);
  return std::make_unique<FakeStreamHandle>(std::move(state));
}

bool FakeStreamProvider::Step(size_t stream_index) {
  const std::shared_ptr<FakeStreamState> state = streams_[stream_index];
  if (state->cancelled || state->paused || state->ended) {
    return false;
  }
  if (!state->opened) {
    state->opened = true;
    if (open_failure_.has_value()) {
      state->ended = true;
      static_cast<void>(state->callbacks->OnError(*open_failure_));
      return true;
    }
    penumbra::ByteStreamInfo info;
    info.seekable = seekable_;
    if (size_known_) {
      info.size = content_.size();
    }
    info.content_type = "video/mp4";
    info.final_url = state->request.url;
    static_cast<void>(state->callbacks->OnOpen(std::move(info)));
    return true;
  }
  if (state->next >= content_.size()) {
    state->ended = true;
    static_cast<void>(state->callbacks->OnEnd());
    return true;
  }
  const auto start = static_cast<size_t>(state->next);
  const size_t length = std::min(chunk_bytes_, content_.size() - start);
  penumbra::ByteStreamChunk chunk;
  chunk.data = penumbra::SharedBytes::Copy(
      std::span<const uint8_t>(content_).subspan(start, length));
  state->next += length;
  static_cast<void>(state->callbacks->OnData(std::move(chunk)));
  return true;
}

size_t FakeStreamProvider::StepAll() {
  size_t steps = 0;
  bool progress = true;
  while (progress) {
    progress = false;
    for (size_t i = 0; i < streams_.size(); ++i) {
      if (Step(i)) {
        ++steps;
        progress = true;
      }
    }
  }
  return steps;
}

void FakeStreamProvider::FailOpenWith(absl::Status status) {
  open_failure_ = std::move(status);
}

void FakeStreamProvider::FailStreamAfterOpen(size_t stream_index,
                                             absl::Status status) {
  const std::shared_ptr<FakeStreamState> state = streams_[stream_index];
  state->ended = true;
  static_cast<void>(state->callbacks->OnError(std::move(status)));
}

void FakeStreamProvider::RejectOpens(absl::Status status) {
  reject_ = std::move(status);
}

}
