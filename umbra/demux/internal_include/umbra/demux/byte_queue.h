#ifndef UMBRA_DEMUX_BYTE_QUEUE_H_
#define UMBRA_DEMUX_BYTE_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "penumbra/interface/shared_bytes.h"

namespace umbra::demux {

class ByteQueue {
 public:
  explicit ByteQueue(uint64_t start_offset = 0)
      : start_offset_(start_offset), end_offset_(start_offset) {}

  void Reset(uint64_t offset);
  void Append(penumbra::SharedBytes bytes);

  [[nodiscard]] uint64_t StartOffset() const { return start_offset_; }
  [[nodiscard]] uint64_t EndOffset() const { return end_offset_; }
  [[nodiscard]] size_t size() const {
    return static_cast<size_t>(end_offset_ - start_offset_);
  }

  [[nodiscard]] std::vector<uint8_t> Peek(size_t count) const;
  [[nodiscard]] std::optional<penumbra::SharedBytes> Read(size_t count);
  void Discard(uint64_t count);

 private:
  void CopyOut(size_t count, std::span<uint8_t> destination) const;

  std::deque<penumbra::SharedBytes> chunks_;
  size_t front_consumed_ = 0;
  uint64_t start_offset_ = 0;
  uint64_t end_offset_ = 0;
};

}

#endif
