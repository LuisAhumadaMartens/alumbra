#include "umbra/base/byte_reader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace umbra::base {
namespace {

constexpr std::array<std::uint8_t, 15> kBytes{0x01, 0x02, 0x03, 0x04, 0x05,
                                              0x06, 0x07, 0x08, 0x09, 0x0a,
                                              0x0b, 0x0c, 0x0d, 0x0e, 0xff};

TEST(ByteReaderTest, ReadsBigEndianIntegers) {
  ByteReader reader(kBytes);
  EXPECT_EQ(reader.size(), kBytes.size());
  EXPECT_EQ(*reader.ReadU8(), 0x01);
  EXPECT_EQ(*reader.ReadU16(), 0x0203);
  EXPECT_EQ(*reader.ReadU24(), 0x040506U);
  EXPECT_EQ(*reader.ReadU32(), 0x0708090aU);
  EXPECT_EQ(reader.position(), 10U);
  EXPECT_EQ(reader.remaining(), 5U);
  ByteReader wide{std::span(kBytes).subspan(7)};
  EXPECT_EQ(*wide.ReadU64(), 0x08090a0b0c0d0effULL);
  EXPECT_EQ(wide.remaining(), 0U);
}

TEST(ByteReaderTest, ReadsEveryUnsignedWidth) {
  for (std::size_t width = 1; width <= 8; ++width) {
    ByteReader reader(kBytes);
    std::uint64_t expected = 0;
    for (std::size_t i = 0; i < width; ++i) {
      expected = (expected << 8U) | kBytes.at(i);
    }
    EXPECT_EQ(*reader.ReadUnsigned(width), expected) << width;
    EXPECT_EQ(reader.position(), width);
  }
}

TEST(ByteReaderTest, RejectsUnsupportedWidthsWithoutMoving) {
  ByteReader reader(kBytes);
  for (const std::size_t width : {std::size_t{0}, std::size_t{9}}) {
    const absl::StatusOr<std::uint64_t> value = reader.ReadUnsigned(width);
    EXPECT_EQ(value.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(reader.position(), 0U);
  }
}

TEST(ByteReaderTest, OverrunIsAnErrorAndLeavesThePositionUnchanged) {
  ByteReader reader{std::span(kBytes).first(3)};
  ASSERT_TRUE(reader.Skip(2).ok());
  const absl::StatusOr<std::uint16_t> value = reader.ReadU16();
  EXPECT_EQ(value.status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(value.status().message(),
            "ByteReader: 2 bytes requested at offset 2 with 1 remaining");
  EXPECT_EQ(reader.position(), 2U);
  EXPECT_EQ(reader.ReadU32().status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(reader.ReadU64().status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(reader.ReadBytes(2).status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(reader.Skip(2).code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(reader.position(), 2U);
  EXPECT_EQ(*reader.ReadU8(), 0x03);
  EXPECT_EQ(reader.ReadU8().status().code(), absl::StatusCode::kOutOfRange);
}

TEST(ByteReaderTest, ReadsAndSkipsByteRanges) {
  ByteReader reader(kBytes);
  const absl::StatusOr<std::span<const std::uint8_t>> bytes =
      reader.ReadBytes(3);
  ASSERT_TRUE(bytes.ok());
  EXPECT_EQ(bytes->data(), kBytes.data());
  EXPECT_EQ(bytes->size(), 3U);
  ASSERT_TRUE(reader.Skip(11).ok());
  const absl::StatusOr<std::span<const std::uint8_t>> empty =
      reader.ReadBytes(0);
  ASSERT_TRUE(empty.ok());
  EXPECT_TRUE(empty->empty());
  EXPECT_EQ(*reader.ReadU8(), 0xff);
  EXPECT_TRUE(reader.Skip(0).ok());
  EXPECT_EQ(reader.remaining(), 0U);
}

TEST(ByteReaderTest, EmptyInputFailsEveryRead) {
  ByteReader reader(std::span<const std::uint8_t>{});
  EXPECT_EQ(reader.size(), 0U);
  EXPECT_EQ(reader.ReadU8().status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_TRUE(reader.ReadBytes(0).ok());
  EXPECT_EQ(reader.Skip(1).code(), absl::StatusCode::kOutOfRange);
}

}
}
