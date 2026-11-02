#include "fake_data_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/task_runner.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source::test {

FakeDataSource::FakeDataSource(
    std::vector<uint8_t> content, bool seekable, bool size_known,
    std::shared_ptr<base::SequencedTaskRunner> runner)
    : content_(std::move(content)), runner_(std::move(runner)) {
  info_.seekable = seekable;
  info_.content_type = "application/octet-stream";
  info_.final_url = "https://example.test/media";
  if (size_known) {
    info_.size = content_.size();
  }
}

const DataSourceInfo& FakeDataSource::Info() const { return info_; }

std::shared_ptr<ReadRequest> FakeDataSource::Read(
    uint64_t offset, size_t size, ReadRequest::Callback callback) {
  auto request = ReadRequest::Create(std::move(callback));
  log_.push_back({.offset = offset, .size = size});
  Inflight entry{.offset = offset, .size = size, .request = request};
  if (closed_) {
    entry.failure = absl::FailedPreconditionError("closed");
  } else if (!info_.seekable && offset != next_sequential_) {
    entry.failure = absl::OutOfRangeError("not seekable");
  } else if (next_failure_.has_value()) {
    entry.failure = std::exchange(next_failure_, std::nullopt);
  }
  inflight_.push_back(std::move(entry));
  return request;
}

void FakeDataSource::Close() { closed_ = true; }

void FakeDataSource::FailNext(absl::Status status) {
  next_failure_ = std::move(status);
}

void FakeDataSource::DeliverNext() {
  if (inflight_.empty()) {
    return;
  }
  Inflight entry = std::move(inflight_.front());
  inflight_.pop_front();
  absl::StatusOr<penumbra::SharedBytes> result;
  if (entry.failure.has_value()) {
    result = *entry.failure;
  } else {
    const size_t start =
        static_cast<size_t>(std::min<uint64_t>(entry.offset, content_.size()));
    const size_t length = std::min(entry.size, content_.size() - start);
    next_sequential_ = entry.offset + length;
    result = penumbra::SharedBytes::Copy(
        std::span<const uint8_t>(content_).subspan(start, length));
  }
  static_cast<void>(runner_->PostTask([request = std::move(entry.request),
                                       result = std::move(result)]() mutable {
    request->Complete(std::move(result));
  }));
}

void FakeDataSource::DeliverAll() {
  while (!inflight_.empty()) {
    DeliverNext();
  }
}

}
