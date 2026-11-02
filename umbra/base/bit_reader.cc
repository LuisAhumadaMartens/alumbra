#include "umbra/base/bit_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace umbra::base {
namespace {

constexpr int kMaxReadBits = 64;
constexpr std::size_t kMaxExpGolombLeadingZeros = 31;

}

absl::Status BitReader::Require(std::size_t count) const {
  if (count > remaining()) {
    return absl::OutOfRangeError(
        absl::StrCat("BitReader: ", count, " bits requested at bit offset ",
                     position_, " with ", remaining(), " remaining"));
  }
  return absl::OkStatus();
}

unsigned BitReader::BitAt(std::size_t position) const {
  const unsigned byte = data_[position / kBitsPerByte];
  return (byte >> (kBitsPerByte - 1 - (position % kBitsPerByte))) & 1U;
}

absl::StatusOr<std::uint64_t> BitReader::ReadBits(int count) {
  if (count < 0 || count > kMaxReadBits) {
    return absl::InvalidArgumentError(
        absl::StrCat("BitReader: cannot read ", count, " bits at once"));
  }
  const auto bits = static_cast<std::size_t>(count);
  if (absl::Status status = Require(bits); !status.ok()) {
    return status;
  }
  std::uint64_t value = 0;
  std::size_t left = bits;
  while (left > 0) {
    const std::size_t available = kBitsPerByte - (position_ % kBitsPerByte);
    const std::size_t take = std::min(available, left);
    const unsigned byte = data_[position_ / kBitsPerByte];
    const unsigned chunk = (byte >> (available - take)) & ((1U << take) - 1U);
    value = (value << take) | chunk;
    position_ += take;
    left -= take;
  }
  return value;
}

absl::StatusOr<bool> BitReader::ReadFlag() {
  absl::StatusOr<std::uint64_t> bit = ReadBits(1);
  if (!bit.ok()) {
    return bit.status();
  }
  return *bit != 0;
}

absl::Status BitReader::SkipBits(std::size_t count) {
  if (absl::Status status = Require(count); !status.ok()) {
    return status;
  }
  position_ += count;
  return absl::OkStatus();
}

void BitReader::SkipToByteBoundary() {
  position_ = (position_ + kBitsPerByte - 1) / kBitsPerByte * kBitsPerByte;
}

absl::StatusOr<std::uint32_t> BitReader::ReadExpGolomb() {
  const std::size_t start = position_;
  std::size_t leading_zeros = 0;
  while (position_ < BitCount() && BitAt(position_) == 0 &&
         leading_zeros <= kMaxExpGolombLeadingZeros) {
    ++leading_zeros;
    ++position_;
  }
  if (leading_zeros > kMaxExpGolombLeadingZeros) {
    position_ = start;
    return absl::InvalidArgumentError(absl::StrCat(
        "BitReader: Exp-Golomb code at bit offset ", start, " has more than ",
        kMaxExpGolombLeadingZeros, " leading zero bits"));
  }
  if (!Require(leading_zeros + 1).ok()) {
    position_ = start;
    return absl::OutOfRangeError(absl::StrCat(
        "BitReader: Exp-Golomb code at bit offset ", start, " is truncated"));
  }
  ++position_;
  const absl::StatusOr<std::uint64_t> suffix =
      ReadBits(static_cast<int>(leading_zeros));
  if (!suffix.ok()) {
    position_ = start;
    return suffix.status();
  }
  return static_cast<std::uint32_t>((std::uint64_t{1} << leading_zeros) - 1 +
                                    *suffix);
}

absl::StatusOr<std::int32_t> BitReader::ReadSignedExpGolomb() {
  const absl::StatusOr<std::uint32_t> code = ReadExpGolomb();
  if (!code.ok()) {
    return code.status();
  }
  const std::int64_t magnitude = (static_cast<std::int64_t>(*code) + 1) / 2;
  return static_cast<std::int32_t>(*code % 2 == 1 ? magnitude : -magnitude);
}

}
