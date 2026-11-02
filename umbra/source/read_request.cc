#include "umbra/source/read_request.h"

#include <cassert>
#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"

namespace umbra::source {

std::shared_ptr<ReadRequest> ReadRequest::Create(Callback callback) {
  return std::make_shared<ReadRequest>(std::move(callback));
}

ReadRequest::ReadRequest(Callback callback) : callback_(std::move(callback)) {
  sequence_.DetachFromSequence();
}

void ReadRequest::Cancel() {
  assert(sequence_.CalledOnValidSequence());
  if (state_ != State::kPending) {
    return;
  }
  state_ = State::kCancelled;
  callback_ = nullptr;
}

bool ReadRequest::Complete(absl::StatusOr<penumbra::SharedBytes> result) {
  assert(sequence_.CalledOnValidSequence());
  if (state_ != State::kPending) {
    return false;
  }
  state_ = State::kCompleted;
  Callback callback = std::move(callback_);
  callback_ = nullptr;
  std::move(callback)(std::move(result));
  return true;
}

bool ReadRequest::Cancelled() const {
  assert(sequence_.CalledOnValidSequence());
  return state_ == State::kCancelled;
}

bool ReadRequest::Completed() const {
  assert(sequence_.CalledOnValidSequence());
  return state_ == State::kCompleted;
}

}
