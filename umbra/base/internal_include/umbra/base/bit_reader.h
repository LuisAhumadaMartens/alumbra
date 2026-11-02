#ifndef UMBRA_BASE_BIT_READER_H_
#define UMBRA_BASE_BIT_READER_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::base {

class BitReader {
 public:
  explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

  [[nodiscard]] std::size_t position() const { return position_; }
  [[nodiscard]] std::size_t remaining() const { return BitCount() - position_; }
  [[nodiscard]] bool is_byte_aligned() const {
    return position_ % kBitsPerByte == 0;
  }

  [[nodiscard]] absl::StatusOr<std::uint64_t> ReadBits(int count);
  [[nodiscard]] absl::StatusOr<bool> ReadFlag();
  [[nodiscard]] absl::Status SkipBits(std::size_t count);
  void SkipToByteBoundary();
  [[nodiscard]] absl::StatusOr<std::uint32_t> ReadExpGolomb();
  [[nodiscard]] absl::StatusOr<std::int32_t> ReadSignedExpGolomb();

 private:
  static constexpr std::size_t kBitsPerByte = 8;

  [[nodiscard]] std::size_t BitCount() const {
    return data_.size() * kBitsPerByte;
  }
  [[nodiscard]] absl::Status Require(std::size_t count) const;
  [[nodiscard]] unsigned BitAt(std::size_t position) const;

  std::span<const std::uint8_t> data_;
  std::size_t position_ = 0;
};

}

#endif
