#include "umbra/demux/ebml.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "mkv_builder.h"
#include "mp4_builder.h"

namespace umbra::demux::test {
namespace {

TEST(EbmlVintTest, ReadsEveryLength) {
  struct Case {
    Bytes bytes;
    uint64_t value;
    size_t length;
  };
  const std::vector<Case> cases = {
      {{0x81}, 1, 1},
      {{0xFE}, 126, 1},
      {{0x40, 0x7F}, 127, 2},
      {{0x20, 0x00, 0x80}, 128, 3},
      {{0x10, 0x00, 0x00, 0x01}, 1, 4},
      {{0x08, 0x00, 0x00, 0x00, 0x02}, 2, 5},
      {{0x04, 0x00, 0x00, 0x00, 0x00, 0x03}, 3, 6},
      {{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}, 4, 7},
      {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05}, 5, 8},
  };
  for (const Case& item : cases) {
    const auto parsed = ParseVint(item.bytes);
    ASSERT_TRUE(parsed.ok()) << item.length;
    EXPECT_EQ(parsed->value, item.value);
    EXPECT_EQ(parsed->length, item.length);
    EXPECT_FALSE(parsed->all_ones);
  }
}

TEST(EbmlVintTest, AllOnesIsReserved) {
  for (size_t length = 1; length <= 8; ++length) {
    Bytes bytes(length, 0xFF);
    bytes[0] = static_cast<uint8_t>(0xFFU >> (length - 1));
    const auto parsed = ParseVint(bytes);
    ASSERT_TRUE(parsed.ok()) << length;
    EXPECT_TRUE(parsed->all_ones) << length;
  }
}

TEST(EbmlVintTest, RejectsTruncatedAndInvalidMarkers) {
  EXPECT_EQ(ParseVint(Bytes()).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseVint(Bytes{0x00, 0x01}).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseVint(Bytes{0x40}).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(EbmlHeaderTest, ParsesIdsAndSizes) {
  const Bytes bytes = Element(kIdSegment, Bytes{1, 2, 3});
  const auto header = ParseEbmlHeader(bytes);
  ASSERT_TRUE(header.ok());
  EXPECT_EQ(header->id, kIdSegment);
  EXPECT_EQ(header->size, 3U);
  EXPECT_EQ(header->header_size, 5U);
  EXPECT_FALSE(header->UnknownSize());
}

TEST(EbmlHeaderTest, UnknownSizeIsRecognised) {
  const Bytes bytes = UnknownSizeElement(kIdCluster, Bytes());
  const auto header = ParseEbmlHeader(bytes);
  ASSERT_TRUE(header.ok());
  EXPECT_TRUE(header->UnknownSize());
  EXPECT_EQ(header->header_size, 12U);
}

TEST(EbmlHeaderTest, ReportsHowManyBytesItNeeds) {
  const Bytes bytes = Element(kIdSegment, Bytes());
  for (size_t length = 0; length < 5; ++length) {
    const Bytes prefix(bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const auto needed = EbmlHeaderBytes(prefix);
    ASSERT_TRUE(needed.ok()) << length;
    EXPECT_GT(*needed, length);
    EXPECT_LE(*needed, 5U);
  }
  EXPECT_EQ(*EbmlHeaderBytes(bytes), 5U);
}

TEST(EbmlHeaderTest, RejectsLongIdsAndSizes) {
  EXPECT_EQ(EbmlHeaderBytes(Bytes{0x08, 0x00}).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(EbmlHeaderBytes(Bytes{0x00}).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(EbmlHeaderBytes(Bytes{0x81, 0x00}).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseEbmlHeader(Bytes{0x81}).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(EbmlReaderTest, WalksChildrenAndStopsAtTheEnd) {
  Writer writer;
  writer.Add(UintElement(0xE7, 5));
  writer.Add(StringElement(0x86, "abc"));
  const Bytes bytes = writer.Data();
  EbmlReader reader(bytes);
  const auto first = reader.Next();
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(first->id, 0xE7U);
  EXPECT_EQ(*ReadEbmlUnsigned(first->payload), 5U);
  const auto second = reader.Next();
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(*ReadEbmlString(second->payload), "abc");
  EXPECT_TRUE(reader.AtEnd());
}

TEST(EbmlReaderTest, RejectsChildrenLargerThanTheParent) {
  Bytes bytes = Element(0xE7, Bytes{1, 2, 3});
  bytes.pop_back();
  EbmlReader reader(bytes);
  EXPECT_EQ(reader.Next().status().code(), absl::StatusCode::kDataLoss);
}

TEST(EbmlReaderTest, RejectsUnknownSizeInsideAMaster) {
  const Bytes bytes = UnknownSizeElement(kIdCluster, Bytes{1});
  EbmlReader reader(bytes);
  EXPECT_EQ(reader.Next().status().code(), absl::StatusCode::kDataLoss);
}

TEST(EbmlValueTest, UnsignedHandlesEveryWidth) {
  EXPECT_EQ(*ReadEbmlUnsigned(Bytes()), 0U);
  EXPECT_EQ(*ReadEbmlUnsigned(Bytes{0x01, 0x00}), 256U);
  EXPECT_EQ(*ReadEbmlUnsigned(Bytes(8, 0xFF)), ~uint64_t{0});
  EXPECT_FALSE(ReadEbmlUnsigned(Bytes(9, 0)).ok());
}

TEST(EbmlValueTest, SignedExtendsTheSign) {
  EXPECT_EQ(*ReadEbmlSigned(Bytes()), 0);
  EXPECT_EQ(*ReadEbmlSigned(Bytes{0xFF}), -1);
  EXPECT_EQ(*ReadEbmlSigned(Bytes{0x7F}), 127);
  EXPECT_EQ(*ReadEbmlSigned(Bytes{0xFF, 0x00}), -256);
  EXPECT_FALSE(ReadEbmlSigned(Bytes(9, 0)).ok());
}

TEST(EbmlValueTest, FloatsAreFourOrEightBytes) {
  EXPECT_EQ(*ReadEbmlFloat(Bytes()), 0.0);
  EXPECT_EQ(*ReadEbmlFloat(Bytes{0x3F, 0x80, 0x00, 0x00}), 1.0);
  EXPECT_EQ(*ReadEbmlFloat(Bytes{0x40, 0xE7, 0x70, 0, 0, 0, 0, 0}), 48000.0);
  EXPECT_FALSE(ReadEbmlFloat(Bytes{1, 2, 3}).ok());
}

TEST(EbmlValueTest, StringsStopAtTheFirstNul) {
  EXPECT_EQ(*ReadEbmlString(Bytes{'w', 'e', 'b', 'm', 0, 0}), "webm");
  EXPECT_EQ(*ReadEbmlString(Bytes()), "");
  EXPECT_FALSE(ReadEbmlString(Bytes(5000, 'a')).ok());
}

TEST(EbmlIdTest, LevelOneIds) {
  EXPECT_TRUE(IsLevelOneId(kIdCluster));
  EXPECT_TRUE(IsLevelOneId(kIdCues));
  EXPECT_TRUE(IsSegmentChildId(kIdTracks));
  EXPECT_FALSE(IsSegmentChildId(kIdSegment));
  EXPECT_FALSE(IsLevelOneId(kIdSimpleBlock));
}

}
}
