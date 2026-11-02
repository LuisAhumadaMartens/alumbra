#include "umbra/demux/caption_extractor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace umbra::demux {
namespace {

using Bytes = std::vector<uint8_t>;

Bytes Ga94(const Bytes& cc_data) {
  Bytes payload = {0xB5, 0x00, 0x31, 'G', 'A', '9', '4', 0x03};
  payload.insert(payload.end(), cc_data.begin(), cc_data.end());
  return payload;
}

Bytes SeiMessage(uint32_t type, const Bytes& payload) {
  Bytes message;
  uint32_t value = type;
  while (value >= 255) {
    message.push_back(0xFF);
    value -= 255;
  }
  message.push_back(static_cast<uint8_t>(value));
  size_t size = payload.size();
  while (size >= 255) {
    message.push_back(0xFF);
    size -= 255;
  }
  message.push_back(static_cast<uint8_t>(size));
  message.insert(message.end(), payload.begin(), payload.end());
  return message;
}

Bytes WithTrailingBits(Bytes bytes) {
  bytes.push_back(0x80);
  return bytes;
}

const Bytes kTwoTriplets = {0x42, 0xFF, 0xFC, 0x80, 0x80,
                            0xFD, 0x81, 0x82, 0xFF};

TEST(CaptionExtractorTest, ReadsTripletsFromARegisteredT35Message) {
  const auto result = ExtractCaptionDataFromSei(
      WithTrailingBits(SeiMessage(4, Ga94(kTwoTriplets))));
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0], (Bytes{0xFC, 0x80, 0x80, 0xFD, 0x81, 0x82}));
}

TEST(CaptionExtractorTest, SkipsOtherMessagesAndForeignPayloads) {
  Bytes sei = SeiMessage(5, Bytes(300, 7));
  const Bytes other =
      SeiMessage(4, Bytes{0xB5, 0x00, 0x31, 'D', 'T', 'G', '1', 0x03, 0x42});
  const Bytes wanted = SeiMessage(4, Ga94(kTwoTriplets));
  sei.insert(sei.end(), other.begin(), other.end());
  sei.insert(sei.end(), wanted.begin(), wanted.end());
  const auto result = ExtractCaptionDataFromSei(WithTrailingBits(sei));
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].size(), 6U);
}

TEST(CaptionExtractorTest, IgnoresDataWithoutTheProcessFlag) {
  Bytes cc = kTwoTriplets;
  cc[0] = 0x02;
  EXPECT_TRUE(
      ExtractCaptionDataFromSei(WithTrailingBits(SeiMessage(4, Ga94(cc))))
          .empty());
}

TEST(CaptionExtractorTest, IgnoresTruncatedMessagesWithoutFailing) {
  Bytes sei = WithTrailingBits(SeiMessage(4, Ga94(kTwoTriplets)));
  sei.resize(sei.size() - 6);
  EXPECT_TRUE(ExtractCaptionDataFromSei(sei).empty());
  EXPECT_TRUE(ExtractCaptionDataFromSei(Bytes{}).empty());
  EXPECT_TRUE(ExtractCaptionDataFromSei(Bytes(40, 0xFF)).empty());
}

TEST(CaptionExtractorTest, FindsSeiInLengthPrefixedH264Samples) {
  Bytes sei_nal = {0x06};
  const Bytes message = WithTrailingBits(SeiMessage(4, Ga94(kTwoTriplets)));
  sei_nal.insert(sei_nal.end(), message.begin(), message.end());
  Bytes sample = {0, 0, 0, static_cast<uint8_t>(sei_nal.size())};
  sample.insert(sample.end(), sei_nal.begin(), sei_nal.end());
  const Bytes slice = {0x65, 1, 2, 3};
  sample.insert(sample.end(), {0, 0, 0, 4});
  sample.insert(sample.end(), slice.begin(), slice.end());
  const auto result = ExtractCaptionData(CaptionCodec::kH264, 4, sample);
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].size(), 6U);
  EXPECT_TRUE(ExtractCaptionData(CaptionCodec::kH264, 4, slice).empty());
}

TEST(CaptionExtractorTest, FindsPrefixAndSuffixSeiInHevcSamples) {
  const Bytes message = WithTrailingBits(SeiMessage(4, Ga94(kTwoTriplets)));
  Bytes prefix = {0x4E, 0x01};
  prefix.insert(prefix.end(), message.begin(), message.end());
  Bytes suffix = {0x50, 0x01};
  suffix.insert(suffix.end(), message.begin(), message.end());
  Bytes sample;
  for (const Bytes* nal : {&prefix, &suffix}) {
    sample.insert(sample.end(), {0, 0, static_cast<uint8_t>(nal->size() >> 8),
                                 static_cast<uint8_t>(nal->size())});
    sample.insert(sample.end(), nal->begin(), nal->end());
  }
  EXPECT_EQ(ExtractCaptionData(CaptionCodec::kHevc, 4, sample).size(), 2U);
}

TEST(CaptionExtractorTest, UnescapesEmulationPreventionBytes) {
  const Bytes cc = {0x42, 0xFF, 0xFC, 0x00, 0x00, 0xFD, 0x81, 0x82, 0xFF};
  Bytes message = WithTrailingBits(SeiMessage(4, Ga94(cc)));
  const Bytes needle = {0xFC, 0x00, 0x00};
  const auto at = std::ranges::search(message, needle).begin();
  ASSERT_NE(at, message.end());
  message.insert(at + 3, 0x03);
  Bytes nal = {0x06};
  nal.insert(nal.end(), message.begin(), message.end());
  Bytes sample = {0, 0, 0, static_cast<uint8_t>(nal.size())};
  sample.insert(sample.end(), nal.begin(), nal.end());
  const auto result = ExtractCaptionData(CaptionCodec::kH264, 4, sample);
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0], (Bytes{0xFC, 0x00, 0x00, 0xFD, 0x81, 0x82}));
}

TEST(CaptionExtractorTest, MalformedNalLengthsYieldNothing) {
  EXPECT_TRUE(
      ExtractCaptionData(CaptionCodec::kH264, 4, Bytes{0, 0, 1, 0, 6}).empty());
  EXPECT_TRUE(ExtractCaptionData(CaptionCodec::kH264, 4, Bytes{}).empty());
}

TEST(CaptionExtractorTest, BoundsTheMessagesReadPerSample) {
  Bytes sei;
  for (int index = 0; index < 200; ++index) {
    const Bytes message = SeiMessage(4, Ga94(kTwoTriplets));
    sei.insert(sei.end(), message.begin(), message.end());
  }
  EXPECT_LE(ExtractCaptionDataFromSei(WithTrailingBits(sei)).size(),
            kMaxCaptionMessagesPerSample);
}

}
}
