#ifndef UMBRA_CODEC_READER_UTIL_H_
#define UMBRA_CODEC_READER_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/bit_reader.h"
#include "umbra/base/byte_reader.h"

namespace umbra::codec {

using Bytes = std::span<const std::uint8_t>;

[[nodiscard]] absl::Status Corrupt(std::string_view what);
[[nodiscard]] absl::Status Unsupported(std::string_view what);
[[nodiscard]] absl::Status InvalidCodecString(std::string_view what);

[[nodiscard]] absl::Status Within(absl::Status status, std::string_view what);

template <typename T>
[[nodiscard]] absl::StatusOr<T> Within(absl::StatusOr<T> value,
                                       std::string_view what) {
  if (value.ok()) {
    return value;
  }
  return Within(value.status(), what);
}

class RecordReader {
 public:
  RecordReader(Bytes data, std::string_view what)
      : reader_(data), what_(what) {}

  [[nodiscard]] std::size_t remaining() const { return reader_.remaining(); }
  [[nodiscard]] std::size_t position() const { return reader_.position(); }

  [[nodiscard]] absl::StatusOr<std::uint8_t> U8() {
    return Within(reader_.ReadU8(), what_);
  }
  [[nodiscard]] absl::StatusOr<std::uint16_t> U16() {
    return Within(reader_.ReadU16(), what_);
  }
  [[nodiscard]] absl::StatusOr<std::uint32_t> U24() {
    return Within(reader_.ReadU24(), what_);
  }
  [[nodiscard]] absl::StatusOr<std::uint32_t> U32() {
    return Within(reader_.ReadU32(), what_);
  }
  [[nodiscard]] absl::StatusOr<std::uint64_t> Unsigned(std::size_t bytes) {
    return Within(reader_.ReadUnsigned(bytes), what_);
  }
  [[nodiscard]] absl::StatusOr<Bytes> Take(std::size_t count) {
    return Within(reader_.ReadBytes(count), what_);
  }
  [[nodiscard]] absl::Status Skip(std::size_t count) {
    return Within(reader_.Skip(count), what_);
  }

 private:
  base::ByteReader reader_;
  std::string what_;
};

class BitstreamReader {
 public:
  BitstreamReader(Bytes data, std::string_view what)
      : reader_(data), what_(what) {}

  [[nodiscard]] bool Ok() const { return status_.ok(); }
  [[nodiscard]] const absl::Status& FirstError() const { return status_; }
  [[nodiscard]] std::size_t remaining() const { return reader_.remaining(); }

  [[nodiscard]] std::uint32_t Bits(int count);
  [[nodiscard]] std::uint64_t Bits64(int count);
  [[nodiscard]] bool Flag();
  void Skip(std::size_t count);
  void AlignToByte() { reader_.SkipToByteBoundary(); }
  [[nodiscard]] std::uint32_t Ue();
  [[nodiscard]] std::int32_t Se();
  [[nodiscard]] std::uint32_t UeAtMost(std::uint32_t limit);
  [[nodiscard]] std::uint32_t BitsAtMost(int count, std::uint32_t limit);
  void Fail(std::string_view reason);

 private:
  void Latch(const absl::Status& status);

  base::BitReader reader_;
  std::string what_;
  absl::Status status_;
};

}

#endif
