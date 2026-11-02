#ifndef UMBRA_BASE_BYTE_READER_H_
#define UMBRA_BASE_BYTE_READER_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::base {

class ByteReader {
 public:
  explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

  [[nodiscard]] std::size_t size() const { return data_.size(); }
  [[nodiscard]] std::size_t position() const { return position_; }
  [[nodiscard]] std::size_t remaining() const {
    return data_.size() - position_;
  }

  [[nodiscard]] absl::StatusOr<std::uint8_t> ReadU8();
  [[nodiscard]] absl::StatusOr<std::uint16_t> ReadU16();
  [[nodiscard]] absl::StatusOr<std::uint32_t> ReadU24();
  [[nodiscard]] absl::StatusOr<std::uint32_t> ReadU32();
  [[nodiscard]] absl::StatusOr<std::uint64_t> ReadU64();
  [[nodiscard]] absl::StatusOr<std::uint64_t> ReadUnsigned(
      std::size_t byte_count);
  [[nodiscard]] absl::StatusOr<std::span<const std::uint8_t>> ReadBytes(
      std::size_t count);
  [[nodiscard]] absl::Status Skip(std::size_t count);

 private:
  [[nodiscard]] absl::Status Require(std::size_t count) const;

  std::span<const std::uint8_t> data_;
  std::size_t position_ = 0;
};

}

#endif
