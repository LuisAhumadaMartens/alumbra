#include "umbra/source/byte_stream_data_source.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"
#include "umbra/source/sequenced_receiver.h"
#include "umbra/source/source_error.h"

namespace umbra::source {
namespace {

class StreamReceiver final : public penumbra::ByteStreamCallbacks {
 public:
  explicit StreamReceiver(
      std::shared_ptr<SequencedReceiver<ByteStreamDataSource>> receiver)
      : receiver_(std::move(receiver)) {}

  absl::Status OnOpen(penumbra::ByteStreamInfo info) override {
    return receiver_->Post(
        [info = std::move(info)](ByteStreamDataSource& target) mutable {
          target.HandleOpen(std::move(info));
        });
  }

  absl::Status OnData(penumbra::ByteStreamChunk chunk) override {
    return receiver_->Post(
        [data = std::move(chunk.data)](ByteStreamDataSource& target) mutable {
          target.HandleData(std::move(data));
        });
  }

  absl::Status OnEnd() override {
    return receiver_->Post(
        [](ByteStreamDataSource& target) { target.HandleEnd(); });
  }

  absl::Status OnError(absl::Status error) override {
    return receiver_->Post(
        [error = std::move(error)](ByteStreamDataSource& target) mutable {
          target.HandleError(error);
        });
  }

 private:
  std::shared_ptr<SequencedReceiver<ByteStreamDataSource>> receiver_;
};

}

std::shared_ptr<OpenRequest> OpenRequest::Create(Callback callback) {
  return std::make_shared<OpenRequest>(std::move(callback));
}

OpenRequest::OpenRequest(Callback callback) : callback_(std::move(callback)) {
  sequence_.DetachFromSequence();
}

void OpenRequest::Retain(std::shared_ptr<DataSource> source) {
  owner_ = std::move(source);
}

void OpenRequest::Cancel() {
  assert(sequence_.CalledOnValidSequence());
  if (state_ != State::kPending) {
    return;
  }
  state_ = State::kCancelled;
  callback_ = nullptr;
  const std::shared_ptr<DataSource> owner = std::move(owner_);
  owner_.reset();
}

bool OpenRequest::Complete(absl::StatusOr<std::shared_ptr<DataSource>> result) {
  assert(sequence_.CalledOnValidSequence());
  if (state_ != State::kPending) {
    return false;
  }
  state_ = State::kCompleted;
  Callback callback = std::move(callback_);
  callback_ = nullptr;
  owner_.reset();
  std::move(callback)(std::move(result));
  return true;
}

bool OpenRequest::Cancelled() const {
  assert(sequence_.CalledOnValidSequence());
  return state_ == State::kCancelled;
}

bool OpenRequest::Completed() const {
  assert(sequence_.CalledOnValidSequence());
  return state_ == State::kCompleted;
}

std::shared_ptr<OpenRequest> ByteStreamDataSource::Open(
    std::shared_ptr<penumbra::ByteStreamProvider> provider,
    penumbra::ByteStreamRequest request,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<const base::UrlRedactor> redactor,
    ByteStreamDataSourceOptions options, OpenRequest::Callback callback) {
  auto open_request = OpenRequest::Create(std::move(callback));
  if (provider == nullptr || runner == nullptr || redactor == nullptr) {
    if (runner != nullptr) {
      static_cast<void>(runner->PostTask([open_request] {
        open_request->Complete(absl::InvalidArgumentError(
            "a provider, a task runner and a redactor are required"));
      }));
    }
    return open_request;
  }
  const uint64_t offset = request.offset;
  const std::shared_ptr<ByteStreamDataSource> source(new ByteStreamDataSource(
      std::move(provider), std::move(request), std::move(runner),
      std::move(redactor), options));
  source->open_request_ = open_request;
  source->open_pending_ = true;
  open_request->Retain(source);
  source->StartStream(offset);
  return open_request;
}

ByteStreamDataSource::ByteStreamDataSource(
    std::shared_ptr<penumbra::ByteStreamProvider> provider,
    penumbra::ByteStreamRequest request,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    std::shared_ptr<const base::UrlRedactor> redactor,
    ByteStreamDataSourceOptions options)
    : provider_(std::move(provider)),
      request_(std::move(request)),
      runner_(std::move(runner)),
      redactor_(std::move(redactor)),
      options_(options) {
  sequence_.DetachFromSequence();
}

ByteStreamDataSource::~ByteStreamDataSource() { Close(); }

const DataSourceInfo& ByteStreamDataSource::Info() const { return info_; }

size_t ByteStreamDataSource::QueuedBytes() const { return queued_; }

bool ByteStreamDataSource::Paused() const { return paused_; }

void ByteStreamDataSource::StopStream() {
  if (receiver_ != nullptr) {
    receiver_->Cancel();
    receiver_.reset();
  }
  stream_.reset();
  paused_ = false;
}

void ByteStreamDataSource::StartStream(uint64_t offset) {
  assert(sequence_.CalledOnValidSequence());
  StopStream();
  queue_.clear();
  front_skip_ = 0;
  queued_ = 0;
  head_ = offset;
  tail_ = offset;
  discard_until_ = offset;
  stream_error_ = absl::OkStatus();
  ended_ = false;
  dead_ = false;
  receiver_ = SequencedReceiver<ByteStreamDataSource>::Create(runner_, this);
  penumbra::ByteStreamRequest request = request_;
  request.offset = offset;
  absl::StatusOr<std::unique_ptr<penumbra::ByteStream>> stream =
      provider_->Open(std::move(request),
                      std::make_shared<StreamReceiver>(receiver_));
  if (!stream.ok()) {
    receiver_->Cancel();
    receiver_.reset();
    HandleError(stream.status());
    return;
  }
  stream_ = *std::move(stream);
}

void ByteStreamDataSource::HandleOpen(penumbra::ByteStreamInfo info) {
  assert(sequence_.CalledOnValidSequence());
  if (closed_ || opened_) {
    return;
  }
  opened_ = true;
  info_.size = info.size;
  info_.seekable = info.seekable;
  info_.content_type = std::move(info.content_type);
  info_.final_url = std::move(info.final_url);
  if (open_pending_) {
    open_pending_ = false;
    if (const std::shared_ptr<OpenRequest> request = open_request_.lock()) {
      request->Complete(std::shared_ptr<DataSource>(shared_from_this()));
    }
  }
}

void ByteStreamDataSource::FailOpen(absl::Status error) {
  open_pending_ = false;
  closed_ = true;
  StopStream();
  if (std::shared_ptr<OpenRequest> request = open_request_.lock()) {
    static_cast<void>(runner_->PostTask(
        [request = std::move(request), error = std::move(error)]() mutable {
          request->Complete(std::move(error));
        }));
  }
}

void ByteStreamDataSource::Append(penumbra::SharedBytes chunk) {
  if (tail_ < discard_until_) {
    const uint64_t skip =
        std::min<uint64_t>(chunk.size(), discard_until_ - tail_);
    tail_ += skip;
    if (queue_.empty()) {
      head_ = tail_;
    }
    std::optional<penumbra::SharedBytes> rest = chunk.Slice(
        static_cast<size_t>(skip), chunk.size() - static_cast<size_t>(skip));
    if (!rest.has_value() || rest->empty()) {
      return;
    }
    chunk = *std::move(rest);
  }
  tail_ += chunk.size();
  queued_ += chunk.size();
  queue_.push_back(std::move(chunk));
}

void ByteStreamDataSource::DiscardTo(uint64_t offset) {
  while (head_ < offset && !queue_.empty()) {
    const size_t front_size = queue_.front().size() - front_skip_;
    const uint64_t wanted = offset - head_;
    if (wanted >= front_size) {
      head_ += front_size;
      queued_ -= front_size;
      queue_.pop_front();
      front_skip_ = 0;
    } else {
      front_skip_ += static_cast<size_t>(wanted);
      head_ += wanted;
      queued_ -= static_cast<size_t>(wanted);
    }
  }
}

penumbra::SharedBytes ByteStreamDataSource::Take(size_t size) {
  if (size == 0) {
    return {};
  }
  penumbra::SharedBytes result;
  const size_t first = queue_.front().size() - front_skip_;
  if (first >= size) {
    result = queue_.front().Slice(front_skip_, size).value_or(result);
  } else {
    std::vector<uint8_t> joined;
    joined.reserve(size);
    size_t skip = front_skip_;
    for (const penumbra::SharedBytes& chunk : queue_) {
      const size_t take = std::min(chunk.size() - skip, size - joined.size());
      const auto view = chunk.span().subspan(skip, take);
      joined.insert(joined.end(), view.begin(), view.end());
      skip = 0;
      if (joined.size() == size) {
        break;
      }
    }
    result = penumbra::SharedBytes::Adopt(std::move(joined));
  }
  DiscardTo(head_ + size);
  return result;
}

void ByteStreamDataSource::HandleData(penumbra::SharedBytes data) {
  assert(sequence_.CalledOnValidSequence());
  if (closed_ || data.empty()) {
    return;
  }
  Append(std::move(data));
  ServePending();
  UpdateFlow();
}

void ByteStreamDataSource::HandleEnd() {
  assert(sequence_.CalledOnValidSequence());
  if (closed_) {
    return;
  }
  ended_ = true;
  if (open_pending_) {
    HandleOpen(penumbra::ByteStreamInfo{});
  }
  ServePending();
}

void ByteStreamDataSource::HandleError(const absl::Status& error) {
  assert(sequence_.CalledOnValidSequence());
  if (closed_) {
    return;
  }
  absl::Status mapped = MapError(error);
  if (open_pending_) {
    FailOpen(std::move(mapped));
    return;
  }
  stream_error_ = std::move(mapped);
  ended_ = true;
  dead_ = true;
  ServePending();
}

void ByteStreamDataSource::Deliver(
    const std::shared_ptr<ReadRequest>& request,
    absl::StatusOr<penumbra::SharedBytes> result) {
  static_cast<void>(
      runner_->PostTask([request, result = std::move(result)]() mutable {
        request->Complete(std::move(result));
      }));
}

void ByteStreamDataSource::ServePending() {
  if (!pending_.has_value()) {
    return;
  }
  if (pending_->request->Cancelled()) {
    pending_.reset();
    return;
  }
  const Pending pending = *pending_;
  if (pending.offset < head_) {
    return;
  }
  if (pending.offset > tail_) {
    if (ended_) {
      pending_.reset();
      if (!stream_error_.ok()) {
        Deliver(pending.request,
                std::exchange(stream_error_, absl::OkStatus()));
      } else {
        Deliver(pending.request, penumbra::SharedBytes());
      }
    }
    return;
  }
  DiscardTo(pending.offset);
  if (Available() >= pending.size) {
    pending_.reset();
    Deliver(pending.request, Take(pending.size));
  } else if (!stream_error_.ok()) {
    pending_.reset();
    Deliver(pending.request, std::exchange(stream_error_, absl::OkStatus()));
  } else if (ended_) {
    pending_.reset();
    Deliver(pending.request, Take(static_cast<size_t>(Available())));
  }
}

void ByteStreamDataSource::UpdateFlow() {
  if (stream_ == nullptr || ended_ || closed_) {
    return;
  }
  if (!paused_ && !pending_.has_value() &&
      queued_ >= options_.pause_threshold_bytes) {
    paused_ = stream_->Pause().ok();
  } else if (paused_ && (pending_.has_value() ||
                         queued_ < options_.pause_threshold_bytes / 2)) {
    if (stream_->Resume().ok()) {
      paused_ = false;
    }
  }
}

std::shared_ptr<ReadRequest> ByteStreamDataSource::Read(
    uint64_t offset, size_t size, ReadRequest::Callback callback) {
  assert(sequence_.CalledOnValidSequence());
  auto request = ReadRequest::Create(std::move(callback));
  if (pending_.has_value() && pending_->request->Cancelled()) {
    pending_.reset();
  }
  if (closed_) {
    Deliver(request, absl::FailedPreconditionError("the source is closed"));
    return request;
  }
  if (pending_.has_value()) {
    Deliver(request,
            absl::FailedPreconditionError("a read is already pending"));
    return request;
  }
  if (size == 0 || (info_.size.has_value() && offset >= *info_.size)) {
    Deliver(request, penumbra::SharedBytes());
    return request;
  }
  const uint64_t end = offset + size;
  if (!info_.seekable) {
    if (offset != head_) {
      Deliver(request,
              absl::OutOfRangeError("the source is not seekable and accepts "
                                    "only the next sequential offset"));
      return request;
    }
    if (dead_ && end > tail_) {
      Deliver(request,
              absl::FailedPreconditionError("the source stream has failed"));
      return request;
    }
  } else if (offset < head_ ||
             (offset > tail_ && offset - tail_ > options_.skip_forward_bytes &&
              !ended_) ||
             (dead_ && end > tail_)) {
    StartStream(offset);
  } else if (offset > tail_ && !ended_) {
    DiscardTo(tail_);
    head_ = tail_;
    discard_until_ = offset;
  }
  pending_ = Pending{.offset = offset, .size = size, .request = request};
  ServePending();
  UpdateFlow();
  return request;
}

void ByteStreamDataSource::Close() {
  if (closed_ && receiver_ == nullptr && stream_ == nullptr) {
    return;
  }
  closed_ = true;
  StopStream();
  if (pending_.has_value()) {
    pending_->request->Cancel();
    pending_.reset();
  }
  open_pending_ = false;
  queue_.clear();
  queued_ = 0;
}

absl::Status ByteStreamDataSource::MapError(const absl::Status& error) const {
  if (error.ok()) {
    return error;
  }
  const absl::StatusOr<std::optional<base::ErrorDetail>> detail =
      base::GetErrorDetail(error);
  if (detail.ok() && detail->has_value()) {
    return error;
  }
  std::map<std::string, std::string> context = {
      {"url", request_.url},
      {"status", std::string(absl::StatusCodeToString(error.code()))}};
  switch (error.code()) {
    case absl::StatusCode::kNotFound:
      return MakeSourceError(base::ErrorCode::kSourceNotFound,
                             absl::StatusCode::kNotFound, error.message(),
                             std::move(context), *redactor_);
    case absl::StatusCode::kPermissionDenied:
      return MakeSourceError(base::ErrorCode::kSourceAccessDenied,
                             absl::StatusCode::kPermissionDenied,
                             error.message(), std::move(context), *redactor_);
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kFailedPrecondition:
      return MakeSourceError(base::ErrorCode::kSourceNotReadable,
                             absl::StatusCode::kInvalidArgument,
                             error.message(), std::move(context), *redactor_);
    case absl::StatusCode::kDeadlineExceeded:
      return MakeSourceError(base::ErrorCode::kNetworkTimeout,
                             absl::StatusCode::kUnavailable, error.message(),
                             std::move(context), *redactor_);
    case absl::StatusCode::kCancelled:
      return error;
    default:
      return MakeSourceError(base::ErrorCode::kSourceReadFailed,
                             absl::StatusCode::kUnavailable, error.message(),
                             std::move(context), *redactor_);
  }
}

}
