#include "umbra/codec/nal_unit.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_util.h"

namespace umbra::codec {
namespace {

using test_data::FromHex;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

TEST(NalUnitTest, RemovesEmulationPreventionBytes) {
  const std::vector<std::uint8_t> input = FromHex("6700000300000301000002");
  EXPECT_THAT(RemoveEmulationPrevention(input),
              ElementsAreArray(FromHex("67000000000100"
                                       "0002")));
}

TEST(NalUnitTest, KeepsThreeAfterASingleZero) {
  const std::vector<std::uint8_t> input = FromHex("00030003");
  EXPECT_THAT(RemoveEmulationPrevention(input), ElementsAreArray(input));
}

TEST(NalUnitTest, RemovesEmulationPreventionAtTheEnd) {
  const std::vector<std::uint8_t> input = FromHex("a0000003");
  EXPECT_THAT(RemoveEmulationPrevention(input),
              ElementsAreArray(FromHex("a00000")));
}

TEST(NalUnitTest, SplitsAnnexBWithThreeAndFourByteStartCodes) {
  const std::vector<std::uint8_t> stream =
      FromHex("0000000167aabb0000010caa00000001ff00");
  const absl::StatusOr<std::vector<Bytes>> units = SplitAnnexB(stream);
  ASSERT_TRUE(units.ok()) << units.status();
  ASSERT_EQ(units->size(), 3U);
  EXPECT_THAT(std::vector<std::uint8_t>((*units)[0].begin(), (*units)[0].end()),
              ElementsAre(0x67, 0xaa, 0xbb));
  EXPECT_THAT(std::vector<std::uint8_t>((*units)[1].begin(), (*units)[1].end()),
              ElementsAre(0x0c, 0xaa));
  EXPECT_THAT(std::vector<std::uint8_t>((*units)[2].begin(), (*units)[2].end()),
              ElementsAre(0xff));
}

TEST(NalUnitTest, RejectsAnnexBWithoutStartCode) {
  const std::vector<std::uint8_t> stream = FromHex("6742");
  EXPECT_EQ(SplitAnnexB(stream).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(SplitAnnexB({}).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(SplitAnnexB(FromHex("0000")).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(NalUnitTest, ConvertsAnnexBToLengthPrefixedAndBack) {
  const std::vector<std::uint8_t> stream =
      FromHex("000000016742c01e00000001ce");
  for (const std::size_t length_size :
       {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
    const absl::StatusOr<std::vector<std::uint8_t>> prefixed =
        AnnexBToLengthPrefixed(stream, length_size);
    ASSERT_TRUE(prefixed.ok()) << prefixed.status();
    EXPECT_EQ(prefixed->size(), (2 * length_size) + 5);
    const absl::StatusOr<std::vector<std::uint8_t>> back =
        LengthPrefixedToAnnexB(*prefixed, length_size);
    ASSERT_TRUE(back.ok()) << back.status();
    EXPECT_THAT(*back, ElementsAreArray(FromHex("000000016742c01e00000001ce")));
  }
}

TEST(NalUnitTest, WritesBigEndianLengths) {
  const absl::StatusOr<std::vector<std::uint8_t>> prefixed =
      AnnexBToLengthPrefixed(FromHex("00000001aabbcc"), 4);
  ASSERT_TRUE(prefixed.ok());
  EXPECT_THAT(*prefixed, ElementsAre(0, 0, 0, 3, 0xaa, 0xbb, 0xcc));
}

TEST(NalUnitTest, RejectsNalUnitsTooLargeForTheLengthField) {
  std::vector<std::uint8_t> stream = FromHex("00000001");
  stream.insert(stream.end(), 300, 0x55);
  EXPECT_EQ(AnnexBToLengthPrefixed(stream, 1).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_TRUE(AnnexBToLengthPrefixed(stream, 2).ok());
}

TEST(NalUnitTest, RejectsBadLengthSizes) {
  const std::vector<std::uint8_t> data = FromHex("00000001aa");
  EXPECT_EQ(AnnexBToLengthPrefixed(data, 0).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(AnnexBToLengthPrefixed(data, 5).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(SplitLengthPrefixed(data, 0).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(NalUnitTest, OverrunInLengthPrefixedDataIsAnError) {
  EXPECT_EQ(SplitLengthPrefixed(FromHex("00000005aabb"), 4).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(SplitLengthPrefixed(FromHex("000000"), 4).status().code(),
            absl::StatusCode::kDataLoss);
  const absl::StatusOr<std::vector<Bytes>> empty = SplitLengthPrefixed({}, 4);
  ASSERT_TRUE(empty.ok());
  EXPECT_TRUE(empty->empty());
}

}
}
