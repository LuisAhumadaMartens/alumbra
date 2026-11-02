#include "umbra/base/bit_reader.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace umbra::base {
namespace {

std::vector<std::uint8_t> Pack(
    std::span<const std::pair<std::uint64_t, int>> fields) {
  std::vector<std::uint8_t> bytes;
  int used = 8;
  for (const auto& [value, width] : fields) {
    for (int bit = width - 1; bit >= 0; --bit) {
      if (used == 8) {
        bytes.push_back(0);
        used = 0;
      }
      bytes.back() = static_cast<std::uint8_t>(
          bytes.back() | (((value >> bit) & 1U) << (7 - used)));
      ++used;
    }
  }
  return bytes;
}

TEST(BitReaderTest, ReadsBitFieldsMostSignificantBitFirst) {
  constexpr std::array<std::uint8_t, 3> kBytes{0b1011'0011, 0b0101'1100, 0xff};
  BitReader reader(kBytes);
  EXPECT_EQ(reader.remaining(), 24U);
  EXPECT_TRUE(*reader.ReadFlag());
  EXPECT_EQ(*reader.ReadBits(3), 0b011U);
  EXPECT_FALSE(reader.is_byte_aligned());
  EXPECT_EQ(*reader.ReadBits(6), 0b0011'01U);
  EXPECT_EQ(*reader.ReadBits(0), 0U);
  EXPECT_EQ(reader.position(), 10U);
  EXPECT_EQ(*reader.ReadBits(14), 0b011100'1111'1111U);
  EXPECT_EQ(reader.remaining(), 0U);
}

TEST(BitReaderTest, ReadsSixtyFourBitsAcrossUnalignedBytes) {
  constexpr std::array<std::uint8_t, 9> kBytes{0x81, 0x23, 0x45, 0x67, 0x89,
                                               0xab, 0xcd, 0xef, 0x80};
  BitReader reader(kBytes);
  ASSERT_TRUE(reader.SkipBits(1).ok());
  EXPECT_EQ(*reader.ReadBits(64), 0x02468acf13579bdfULL);
  EXPECT_EQ(reader.remaining(), 7U);
}

TEST(BitReaderTest, RejectsInvalidCountsAndOverrunsWithoutMoving) {
  constexpr std::array<std::uint8_t, 2> kBytes{0xaa, 0x55};
  BitReader reader(kBytes);
  ASSERT_TRUE(reader.SkipBits(3).ok());
  EXPECT_EQ(reader.ReadBits(65).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(reader.ReadBits(-1).status().code(),
            absl::StatusCode::kInvalidArgument);
  const absl::StatusOr<std::uint64_t> overrun = reader.ReadBits(14);
  EXPECT_EQ(overrun.status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(overrun.status().message(),
            "BitReader: 14 bits requested at bit offset 3 with 13 remaining");
  EXPECT_EQ(reader.SkipBits(14).code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(reader.position(), 3U);
  EXPECT_EQ(*reader.ReadBits(13), 0b01010'0101'0101U);
  EXPECT_EQ(reader.ReadFlag().status().code(), absl::StatusCode::kOutOfRange);
}

TEST(BitReaderTest, SkipsToTheNextByteBoundary) {
  constexpr std::array<std::uint8_t, 2> kBytes{0x00, 0xc0};
  BitReader reader(kBytes);
  reader.SkipToByteBoundary();
  EXPECT_EQ(reader.position(), 0U);
  ASSERT_TRUE(reader.SkipBits(5).ok());
  reader.SkipToByteBoundary();
  EXPECT_TRUE(reader.is_byte_aligned());
  EXPECT_EQ(*reader.ReadBits(2), 0b11U);
  ASSERT_TRUE(reader.SkipBits(6).ok());
  reader.SkipToByteBoundary();
  EXPECT_EQ(reader.position(), 16U);
}

TEST(BitReaderTest, DecodesExpGolombCodesFromTheH264Table) {
  constexpr std::array<std::pair<std::uint64_t, int>, 9> kCodes{
      {{0b1, 1},
       {0b010, 3},
       {0b011, 3},
       {0b00100, 5},
       {0b00101, 5},
       {0b00110, 5},
       {0b00111, 5},
       {0b0001000, 7},
       {0b0001001, 7}}};
  const std::vector<std::uint8_t> bytes = Pack(kCodes);
  BitReader reader(bytes);
  for (std::uint32_t expected = 0; expected < kCodes.size(); ++expected) {
    EXPECT_EQ(*reader.ReadExpGolomb(), expected);
  }
  BitReader signed_reader(bytes);
  for (const std::int32_t expected : {0, 1, -1, 2, -2, 3, -3, 4, -4}) {
    EXPECT_EQ(*signed_reader.ReadSignedExpGolomb(), expected);
  }
}

TEST(BitReaderTest, DecodesTheLargestThirtyTwoBitExpGolombCodes) {
  constexpr std::array<std::pair<std::uint64_t, int>, 4> kCodes{{
      {0, 31},
      {(std::uint64_t{1} << 32) - 1, 32},
      {0, 31},
      {(std::uint64_t{1} << 32) - 2, 32},
  }};
  const std::vector<std::uint8_t> bytes = Pack(kCodes);
  BitReader reader(bytes);
  EXPECT_EQ(*reader.ReadExpGolomb(), 0xfffffffeU);
  EXPECT_EQ(reader.position(), 63U);
  BitReader signed_reader(bytes);
  EXPECT_EQ(*signed_reader.ReadSignedExpGolomb(), -2147483647);
  EXPECT_EQ(*signed_reader.ReadSignedExpGolomb(), 2147483647);
}

TEST(BitReaderTest, RejectsExpGolombCodesLongerThanThirtyTwoBits) {
  constexpr std::array<std::pair<std::uint64_t, int>, 2> kCodes{
      {{0, 32}, {1, 1}}};
  const std::vector<std::uint8_t> bytes = Pack(kCodes);
  BitReader reader(bytes);
  const absl::StatusOr<std::uint32_t> value = reader.ReadExpGolomb();
  EXPECT_EQ(value.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(reader.position(), 0U);
  EXPECT_EQ(reader.ReadSignedExpGolomb().status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(BitReaderTest, TruncatedExpGolombCodesAreOverruns) {
  for (const std::uint64_t code : {0b0000'0000U, 0b0000'0001U}) {
    const std::array<std::pair<std::uint64_t, int>, 1> fields{{{code, 8}}};
    const std::vector<std::uint8_t> bytes = Pack(fields);
    BitReader reader(bytes);
    const absl::StatusOr<std::uint32_t> value = reader.ReadExpGolomb();
    EXPECT_EQ(value.status().code(), absl::StatusCode::kOutOfRange);
    EXPECT_EQ(reader.position(), 0U);
  }
  BitReader empty(std::span<const std::uint8_t>{});
  EXPECT_EQ(empty.ReadExpGolomb().status().code(),
            absl::StatusCode::kOutOfRange);
}

}
}
