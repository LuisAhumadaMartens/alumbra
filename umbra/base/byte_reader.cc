#include "umbra/base/byte_reader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace umbra::base {
namespace {

constexpr std::size_t kMaxUnsignedBytes = 8;

template <typename T>
absl::StatusOr<T> Narrow(absl::StatusOr<std::uint64_t> value) {
  if (!value.ok()) {
    return std::move(value).status();
  }
  return static_cast<T>(*value);
}

}

absl::Status ByteReader::Require(std::size_t count) const {
  if (count > remaining()) {
    return absl::OutOfRangeError(
        absl::StrCat("ByteReader: ", count, " bytes requested at offset ",
                     position_, " with ", remaining(), " remaining"));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::uint8_t> ByteReader::ReadU8() {
  return Narrow<std::uint8_t>(ReadUnsigned(1));
}

absl::StatusOr<std::uint16_t> ByteReader::ReadU16() {
  return Narrow<std::uint16_t>(ReadUnsigned(2));
}

absl::StatusOr<std::uint32_t> ByteReader::ReadU24() {
  return Narrow<std::uint32_t>(ReadUnsigned(3));
}

absl::StatusOr<std::uint32_t> ByteReader::ReadU32() {
  return Narrow<std::uint32_t>(ReadUnsigned(4));
}

absl::StatusOr<std::uint64_t> ByteReader::ReadU64() { return ReadUnsigned(8); }

absl::StatusOr<std::uint64_t> ByteReader::ReadUnsigned(std::size_t byte_count) {
  if (byte_count == 0 || byte_count > kMaxUnsignedBytes) {
    return absl::InvalidArgumentError(absl::StrCat(
        "ByteReader: cannot read a ", byte_count, "-byte unsigned integer"));
  }
  if (absl::Status status = Require(byte_count); !status.ok()) {
    return status;
  }
  std::uint64_t value = 0;
  for (const std::uint8_t byte : data_.subspan(position_, byte_count)) {
    value = (value << 8U) | byte;
  }
  position_ += byte_count;
  return value;
}

absl::StatusOr<std::span<const std::uint8_t>> ByteReader::ReadBytes(
    std::size_t count) {
  if (absl::Status status = Require(count); !status.ok()) {
    return status;
  }
  const std::span<const std::uint8_t> bytes = data_.subspan(position_, count);
  position_ += count;
  return bytes;
}

absl::Status ByteReader::Skip(std::size_t count) {
  if (absl::Status status = Require(count); !status.ok()) {
    return status;
  }
  position_ += count;
  return absl::OkStatus();
}

}
