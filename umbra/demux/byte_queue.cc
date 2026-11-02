#include "umbra/demux/byte_queue.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "penumbra/interface/shared_bytes.h"

namespace umbra::demux {

void ByteQueue::Reset(uint64_t offset) {
  chunks_.clear();
  front_consumed_ = 0;
  start_offset_ = offset;
  end_offset_ = offset;
}

void ByteQueue::Append(penumbra::SharedBytes bytes) {
  if (bytes.empty()) {
    return;
  }
  end_offset_ += bytes.size();
  chunks_.push_back(std::move(bytes));
}

void ByteQueue::CopyOut(size_t count, std::span<uint8_t> destination) const {
  size_t copied = 0;
  size_t skip = front_consumed_;
  for (const penumbra::SharedBytes& chunk : chunks_) {
    if (copied == count) {
      break;
    }
    const std::span<const uint8_t> bytes = chunk.span().subspan(skip);
    skip = 0;
    const size_t take = std::min(bytes.size(), count - copied);
    std::copy_n(bytes.begin(), take, destination.subspan(copied).begin());
    copied += take;
  }
}

std::vector<uint8_t> ByteQueue::Peek(size_t count) const {
  const size_t available = std::min(count, size());
  std::vector<uint8_t> result(available);
  CopyOut(available, result);
  return result;
}

std::optional<penumbra::SharedBytes> ByteQueue::Read(size_t count) {
  if (count > size()) {
    return std::nullopt;
  }
  if (count == 0) {
    return penumbra::SharedBytes();
  }
  const penumbra::SharedBytes& front = chunks_.front();
  if (front.size() - front_consumed_ >= count) {
    std::optional<penumbra::SharedBytes> slice =
        front.Slice(front_consumed_, count);
    Discard(count);
    return slice;
  }
  std::vector<uint8_t> assembled(count);
  CopyOut(count, assembled);
  Discard(count);
  return penumbra::SharedBytes::Adopt(std::move(assembled));
}

void ByteQueue::Discard(uint64_t count) {
  count = std::min<uint64_t>(count, size());
  start_offset_ += count;
  while (count > 0) {
    const size_t available = chunks_.front().size() - front_consumed_;
    if (count < available) {
      front_consumed_ += static_cast<size_t>(count);
      return;
    }
    count -= available;
    chunks_.pop_front();
    front_consumed_ = 0;
  }
}

}
