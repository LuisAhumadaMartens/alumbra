#include "umbra/demux/mkv_block.h"

#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "mkv_builder.h"
#include "mp4_builder.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux::test {
namespace {

Bytes BlockBytes(const MkvBlockSpec& spec) {
  const Bytes element = SimpleBlockElement(spec);
  const auto header = ParseEbmlHeader(element);
  return {element.begin() + static_cast<std::ptrdiff_t>(header->header_size),
          element.end()};
}

MkvBlockSpec Laced(MkvLacing lacing, const std::vector<size_t>& sizes) {
  MkvBlockSpec spec;
  spec.lacing = lacing;
  for (size_t index = 0; index < sizes.size(); ++index) {
    spec.frames.push_back(FrameBytes(1, index, sizes[index]));
  }
  return spec;
}

TEST(MkvBlockTest, ParsesTheHeaderFlags) {
  MkvBlockSpec spec;
  spec.track = 3;
  spec.relative_ticks = -25;
  spec.key = true;
  spec.frames = {FrameBytes(3, 0, 10)};
  const auto block = ParseBlock(BlockBytes(spec));
  ASSERT_TRUE(block.ok());
  EXPECT_EQ(block->track_number, 3U);
  EXPECT_EQ(block->relative_ticks, -25);
  EXPECT_TRUE(block->key_frame);
  EXPECT_EQ(block->lacing, Lacing::kNone);
  ASSERT_EQ(block->frames.size(), 1U);
  EXPECT_EQ(block->frames[0].offset, 4U);
  EXPECT_EQ(block->frames[0].size, 10U);
}

TEST(MkvBlockTest, ParsesTwoByteTrackNumbers) {
  Bytes bytes = {0x40, 0x81, 0x00, 0x05, 0x00, 0xAA};
  const auto block = ParseBlock(bytes);
  ASSERT_TRUE(block.ok());
  EXPECT_EQ(block->track_number, 129U);
  EXPECT_EQ(block->relative_ticks, 5);
  EXPECT_EQ(block->frames[0].offset, 5U);
}

class LacingSizesTest : public ::testing::TestWithParam<MkvLacing> {};

TEST_P(LacingSizesTest, RecoversEveryFrameSize) {
  const std::vector<size_t> sizes =
      GetParam() == MkvLacing::kFixed
          ? std::vector<size_t>{7, 7, 7}
          : std::vector<size_t>{5, 600, 3, 255, 510, 0, 9};
  const MkvBlockSpec spec = Laced(GetParam(), sizes);
  const Bytes bytes = BlockBytes(spec);
  const auto block = ParseBlock(bytes);
  ASSERT_TRUE(block.ok()) << block.status();
  ASSERT_EQ(block->frames.size(), sizes.size());
  size_t position = block->frames[0].offset;
  for (size_t index = 0; index < sizes.size(); ++index) {
    EXPECT_EQ(block->frames[index].size, sizes[index]) << index;
    EXPECT_EQ(block->frames[index].offset, position) << index;
    position += sizes[index];
  }
  EXPECT_EQ(position, bytes.size());
}

INSTANTIATE_TEST_SUITE_P(Kinds, LacingSizesTest,
                         ::testing::Values(MkvLacing::kXiph, MkvLacing::kEbml,
                                           MkvLacing::kFixed));

TEST(MkvBlockTest, ASingleLacedFrameIsAccepted) {
  for (const MkvLacing lacing :
       {MkvLacing::kXiph, MkvLacing::kEbml, MkvLacing::kFixed}) {
    const auto block = ParseBlock(BlockBytes(Laced(lacing, {12})));
    ASSERT_TRUE(block.ok());
    ASSERT_EQ(block->frames.size(), 1U);
    EXPECT_EQ(block->frames[0].size, 12U);
  }
}

TEST(MkvBlockTest, FixedLacingMustDivideEvenly) {
  Bytes bytes = BlockBytes(Laced(MkvLacing::kFixed, {4, 4, 4}));
  bytes.push_back(0);
  EXPECT_EQ(ParseBlock(bytes).status().code(), absl::StatusCode::kDataLoss);
}

TEST(MkvBlockTest, LacedSizesCannotExceedTheBlock) {
  Bytes xiph = BlockBytes(Laced(MkvLacing::kXiph, {5, 5, 5}));
  xiph.resize(xiph.size() - 8);
  EXPECT_EQ(ParseBlock(xiph).status().code(), absl::StatusCode::kDataLoss);
  Bytes ebml = BlockBytes(Laced(MkvLacing::kEbml, {5, 5, 5}));
  ebml.resize(ebml.size() - 8);
  EXPECT_EQ(ParseBlock(ebml).status().code(), absl::StatusCode::kDataLoss);
}

TEST(MkvBlockTest, EbmlLacingRejectsNegativeSizes) {
  Bytes bytes = {0x81, 0x00, 0x00, 0x06, 0x02, 0x82, 0x40, 0x00, 1, 2, 3, 4};
  EXPECT_EQ(ParseBlock(bytes).status().code(), absl::StatusCode::kDataLoss);
}

TEST(MkvBlockTest, XiphLacingNeedsItsSizeBytes) {
  const Bytes bytes = {0x81, 0x00, 0x00, 0x02, 0x02, 0xFF, 0xFF};
  EXPECT_EQ(ParseBlock(bytes).status().code(), absl::StatusCode::kDataLoss);
}

TEST(MkvBlockTest, RejectsMalformedHeaders) {
  EXPECT_FALSE(ParseBlock(Bytes()).ok());
  EXPECT_FALSE(ParseBlock(Bytes{0x81, 0x00}).ok());
  EXPECT_FALSE(ParseBlock(Bytes{0x80, 0x00, 0x00, 0x00}).ok());
  EXPECT_FALSE(ParseBlock(Bytes{0xFF, 0x00, 0x00, 0x00}).ok());
  EXPECT_FALSE(ParseBlock(Bytes{0x81, 0x00, 0x00, 0x02}).ok());
}

}
}
