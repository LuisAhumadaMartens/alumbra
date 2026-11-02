#include "umbra/source/buffered_reader.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/task_runner.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source {

absl::StatusOr<std::unique_ptr<BufferedReader>> BufferedReader::Create(
    std::shared_ptr<DataSource> source,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    BufferedReaderOptions options) {
  if (source == nullptr || runner == nullptr) {
    return absl::InvalidArgumentError(
        "a source and a task runner are required");
  }
  if (options.read_ahead_bytes < kMinReadAheadBytes ||
      options.read_ahead_bytes > kMaxReadAheadBytes) {
    return absl::InvalidArgumentError(
        "read-ahead bytes must be between 65536 and 67108864");
  }
  if (options.chunk_bytes == 0) {
    return absl::InvalidArgumentError("chunk bytes must be positive");
  }
  return std::unique_ptr<BufferedReader>(
      new BufferedReader(std::move(source), std::move(runner), options));
}

BufferedReader::BufferedReader(
    std::shared_ptr<DataSource> source,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    BufferedReaderOptions options)
    : source_(std::move(source)),
      runner_(std::move(runner)),
      options_(options),
      end_of_source_(source_->Info().size) {
  sequence_.DetachFromSequence();
}

BufferedReader::~BufferedReader() { Close(); }

const DataSourceInfo& BufferedReader::Info() const { return source_->Info(); }

size_t BufferedReader::BufferedBytes() const { return buffer_.size(); }

uint64_t BufferedReader::BufferEnd() const {
  return buffer_start_ + buffer_.size();
}

std::shared_ptr<ReadRequest> BufferedReader::Read(
    uint64_t offset, size_t size, ReadRequest::Callback callback) {
  assert(sequence_.CalledOnValidSequence());
  auto request = ReadRequest::Create(std::move(callback));
  if (pending_.has_value() && pending_->request->Cancelled()) {
    pending_.reset();
  }
  if (closed_) {
    Deliver(request, absl::FailedPreconditionError("the reader is closed"));
  } else if (pending_.has_value()) {
    Deliver(request,
            absl::FailedPreconditionError("a read is already pending"));
  } else if (offset > std::numeric_limits<uint64_t>::max() - size) {
    Deliver(request, absl::InvalidArgumentError("the read range overflows"));
  } else if (size == 0) {
    Deliver(request, penumbra::SharedBytes());
  } else if (!source_->Info().seekable && offset < buffer_start_) {
    Deliver(request,
            absl::OutOfRangeError("the source is not seekable and the "
                                  "requested bytes are no longer buffered"));
  } else {
    pending_ = Pending{.offset = offset, .size = size, .request = request};
    cursor_ = offset;
    Pump();
  }
  return request;
}

void BufferedReader::Close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  if (fetch_ != nullptr) {
    fetch_->Cancel();
    fetch_.reset();
  }
  if (pending_.has_value()) {
    pending_->request->Cancel();
    pending_.reset();
  }
  buffer_.clear();
  buffer_.shrink_to_fit();
  source_->Close();
}

void BufferedReader::Pump() {
  if (closed_) {
    return;
  }
  if (pending_.has_value() && pending_->request->Cancelled()) {
    pending_.reset();
  }
  if (pending_.has_value()) {
    const Pending pending = *pending_;
    const uint64_t offset = pending.offset;
    const uint64_t requested_end = offset + pending.size;
    if (end_of_source_.has_value() && offset >= *end_of_source_) {
      cursor_ = offset;
      Deliver(pending.request, penumbra::SharedBytes());
      pending_.reset();
    } else {
      const bool too_far_ahead =
          offset > BufferEnd() && source_->Info().seekable &&
          offset - BufferEnd() > options_.read_ahead_bytes;
      if (offset < buffer_start_ || too_far_ahead) {
        ResetBufferAt(offset);
      } else if (offset > buffer_start_) {
        TrimBefore(std::min(offset, BufferEnd()));
      }
      const uint64_t limit = end_of_source_.has_value()
                                 ? std::min(requested_end, *end_of_source_)
                                 : requested_end;
      if (BufferEnd() >= limit && offset >= buffer_start_) {
        const std::span<const uint8_t> view(buffer_);
        cursor_ = limit;
        Deliver(pending.request, penumbra::SharedBytes::Copy(view.subspan(
                                     offset - buffer_start_, limit - offset)));
        pending_.reset();
      } else if (!deferred_error_.ok() && fetch_ == nullptr) {
        Deliver(pending.request,
                std::exchange(deferred_error_, absl::OkStatus()));
        pending_.reset();
      }
    }
  }
  MaybeFetch();
}

void BufferedReader::MaybeFetch() {
  if (closed_ || fetch_ != nullptr || !deferred_error_.ok()) {
    return;
  }
  if (end_of_source_.has_value() && BufferEnd() >= *end_of_source_) {
    return;
  }
  uint64_t want_end = cursor_ + options_.read_ahead_bytes;
  uint64_t needed = 0;
  if (pending_.has_value()) {
    const uint64_t requested_end = pending_->offset + pending_->size;
    want_end = std::max(want_end, requested_end);
    needed = requested_end > BufferEnd() ? requested_end - BufferEnd() : 0;
  }
  if (BufferEnd() >= want_end) {
    return;
  }
  uint64_t length = std::min<uint64_t>(
      want_end - BufferEnd(), std::max<uint64_t>(options_.chunk_bytes, needed));
  if (end_of_source_.has_value()) {
    length = std::min(length, *end_of_source_ - BufferEnd());
  }
  const auto requested = static_cast<size_t>(length);
  fetch_ = source_->Read(
      BufferEnd(), requested,
      [this, requested](absl::StatusOr<penumbra::SharedBytes> result) {
        OnFetched(requested, std::move(result));
      });
}

void BufferedReader::OnFetched(size_t requested,
                               absl::StatusOr<penumbra::SharedBytes> result) {
  assert(sequence_.CalledOnValidSequence());
  fetch_.reset();
  if (closed_) {
    return;
  }
  if (!result.ok()) {
    deferred_error_ = result.status();
  } else {
    const std::span<const uint8_t> bytes = result->span();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    if (bytes.size() < requested) {
      end_of_source_ = BufferEnd();
    }
  }
  Pump();
}

void BufferedReader::Deliver(const std::shared_ptr<ReadRequest>& request,
                             absl::StatusOr<penumbra::SharedBytes> result) {
  static_cast<void>(
      runner_->PostTask([request, result = std::move(result)]() mutable {
        request->Complete(std::move(result));
      }));
}

void BufferedReader::ResetBufferAt(uint64_t offset) {
  if (fetch_ != nullptr) {
    fetch_->Cancel();
    fetch_.reset();
  }
  buffer_.clear();
  buffer_start_ = offset;
}

void BufferedReader::TrimBefore(uint64_t offset) {
  if (offset <= buffer_start_) {
    return;
  }
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(
                                                       offset - buffer_start_));
  buffer_start_ = offset;
}

}
