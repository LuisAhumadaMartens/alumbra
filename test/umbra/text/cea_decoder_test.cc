#include "umbra/text/cea_decoder.h"

#include <array>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::text {
namespace {

TEST(CeaDecoderTest, Decodes608PopOnAndSuppressesRepeatedControlCode) {
  CeaDecoder decoder;
  const std::array<uint8_t, 18> data = {0xFC, 0x14, 0x20, 0xFC, 0x14, 0x20,
                                        0xFC, 0x48, 0x49, 0xFC, 0x14, 0x2F,
                                        0xFC, 0x14, 0x2F, 0xFC, 0x00, 0x00};
  const std::vector<CaptionSnapshot> output = decoder.Push(data, 3.5);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output[0].service, 1);
  EXPECT_FALSE(output[0].digital);
  EXPECT_EQ(output[0].time, 3.5);
  EXPECT_EQ(output[0].text, "HI");
  EXPECT_EQ(output[0].rows, 15);
  EXPECT_EQ(output[0].columns, 32);
}

TEST(CeaDecoderTest, Decodes608RollUpAndEraseDisplayedMemory) {
  CeaDecoder decoder;
  const std::array<uint8_t, 15> data = {0xFC, 0x14, 0x25, 0xFC, 0x48,
                                        0x49, 0xFC, 0x14, 0x2D, 0xFC,
                                        0x14, 0x2D, 0xFC, 0x14, 0x2C};
  const std::vector<CaptionSnapshot> output = decoder.Push(data, 1.0);
  ASSERT_FALSE(output.empty());
  EXPECT_EQ(output.back().service, 1);
  EXPECT_EQ(output.back().text, "");
}

TEST(CeaDecoderTest, Decodes608WesternEuropeanCharacter) {
  CeaDecoder decoder;
  const std::array<uint8_t, 12> data = {0xFC, 0x14, 0x20, 0xFC, 0x41, 0x42,
                                        0xFC, 0x12, 0x20, 0xFC, 0x14, 0x2F};
  const std::vector<CaptionSnapshot> output = decoder.Push(data, 1.0);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output[0].text, "AÁ");
}

TEST(CeaDecoderTest, IgnoresMalformedTripletAndResetsState) {
  CeaDecoder decoder;
  const std::array<uint8_t, 2> malformed = {0xFC, 0x14};
  EXPECT_TRUE(decoder.Push(malformed, 0.0).empty());
  decoder.Reset();
  const std::array<uint8_t, 3> padding = {0x00, 0x00, 0x00};
  EXPECT_TRUE(decoder.Push(padding, 0.0).empty());
}

TEST(CeaDecoderTest, DecodesDtvccServiceTextAndWindowAnchor) {
  CeaDecoder decoder;
  const std::array<uint8_t, 6> data = {0xFF, 0x02, 0x21, 0xFE, 0x41, 0x00};
  const std::vector<CaptionSnapshot> output = decoder.Push(data, 2.0);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_TRUE(output[0].digital);
  EXPECT_EQ(output[0].service, 1);
  EXPECT_EQ(output[0].time, 2.0);
  EXPECT_EQ(output[0].text, "A");
}

}
}
