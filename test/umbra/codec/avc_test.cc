#include "umbra/codec/avc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fixtures.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "test_util.h"
#include "umbra/codec/codec_string.h"

namespace umbra::codec {
namespace {

using test_data::BitWriter;
using test_data::FromHex;
using test_data::WithEmulationPrevention;
using ::testing::ElementsAreArray;

struct SpsSpec {
  std::uint8_t profile = 66;
  std::uint8_t level = 30;
  std::uint32_t width_mbs_minus1 = 44;
  std::uint32_t height_units_minus1 = 35;
  bool frame_mbs_only = true;
  std::optional<std::array<std::uint32_t, 4>> crop;
  std::optional<std::pair<std::uint32_t, std::uint32_t>> sar;
  std::optional<std::uint32_t> aspect_indicator;
  bool timing = false;
  bool colour = false;
};

std::vector<std::uint8_t> MakeSps(const SpsSpec& spec) {
  BitWriter bits;
  bits.Put(spec.profile, 8).Put(0xc0, 8).Put(spec.level, 8).Ue(0);
  if (spec.profile == 100) {
    bits.Ue(1).Ue(0).Ue(0).Flag(false).Flag(false);
  }
  bits.Ue(0).Ue(0).Ue(0).Ue(4).Flag(false);
  bits.Ue(spec.width_mbs_minus1).Ue(spec.height_units_minus1);
  bits.Flag(spec.frame_mbs_only);
  if (!spec.frame_mbs_only) {
    bits.Flag(false);
  }
  bits.Flag(true);
  bits.Flag(spec.crop.has_value());
  if (spec.crop.has_value()) {
    for (const std::uint32_t offset : *spec.crop) {
      bits.Ue(offset);
    }
  }
  const bool vui =
      spec.sar || spec.aspect_indicator || spec.timing || spec.colour;
  bits.Flag(vui);
  if (vui) {
    bits.Flag(spec.sar.has_value() || spec.aspect_indicator.has_value());
    if (spec.sar) {
      bits.Put(255, 8).Put(spec.sar->first, 16).Put(spec.sar->second, 16);
    } else if (spec.aspect_indicator) {
      bits.Put(*spec.aspect_indicator, 8);
    }
    bits.Flag(false);
    bits.Flag(spec.colour);
    if (spec.colour) {
      bits.Put(5, 3).Flag(true).Flag(true).Put(9, 8).Put(16, 8).Put(9, 8);
    }
    bits.Flag(false);
    bits.Flag(spec.timing);
    if (spec.timing) {
      bits.Put(1001, 32).Put(60000, 32).Flag(true);
    }
  }
  std::vector<std::uint8_t> nal{0x67};
  const std::vector<std::uint8_t> payload =
      WithEmulationPrevention(bits.Rbsp());
  nal.insert(nal.end(), payload.begin(), payload.end());
  return nal;
}

TEST(AvcTest, ParsesTheAvcCRecordOfAnX264Stream) {
  const std::vector<std::uint8_t> record = FromHex(test_data::kAvcC1080);
  const absl::StatusOr<AvcConfig> config = ParseAvcC(record);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->profile_idc, 100);
  EXPECT_EQ(config->profile_compatibility, 0);
  EXPECT_EQ(config->level_idc, 40);
  EXPECT_EQ(config->nal_length_size, 4);
  ASSERT_EQ(config->sps.size(), 1U);
  ASSERT_EQ(config->pps.size(), 1U);
  EXPECT_EQ(config->sps[0].size(), 27U);
  EXPECT_EQ(config->pps[0].size(), 6U);
  EXPECT_EQ(config->chroma_format, 1);
  EXPECT_EQ(config->bit_depth_luma, 8);
  EXPECT_EQ(config->bit_depth_chroma, 8);
  EXPECT_TRUE(config->sps_ext.empty());
}

TEST(AvcTest, GeneratesTheCodecStringFromTheRecord) {
  const absl::StatusOr<AvcConfig> config =
      ParseAvcC(FromHex(test_data::kAvcC1080));
  ASSERT_TRUE(config.ok());
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "avc1.640028");
  EXPECT_EQ(FormatCodecString(AvcCodec{.profile_idc = 0x64, .level_idc = 0x20}),
            "avc1.640020");
}

TEST(AvcTest, SerializesTheRecordBackToTheSameBytes) {
  for (const auto record : {test_data::kAvcC1080, test_data::kAvcC1078}) {
    const std::vector<std::uint8_t> bytes = FromHex(record);
    const absl::StatusOr<AvcConfig> config = ParseAvcC(bytes);
    ASSERT_TRUE(config.ok());
    const absl::StatusOr<std::vector<std::uint8_t>> again =
        SerializeAvcC(*config);
    ASSERT_TRUE(again.ok()) << again.status();
    EXPECT_THAT(*again, ElementsAreArray(bytes));
  }
}

TEST(AvcTest, ParsesTheSpsIncludingEmulationPrevention) {
  const absl::StatusOr<AvcConfig> config =
      ParseAvcC(FromHex(test_data::kAvcC1080));
  ASSERT_TRUE(config.ok());
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(config->sps[0]);
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->profile_idc, 100);
  EXPECT_EQ(sps->level_idc, 40);
  EXPECT_EQ(sps->chroma_format_idc, 1);
  EXPECT_EQ(sps->bit_depth_luma, 8);
  EXPECT_EQ(sps->coded_width, 1920);
  EXPECT_EQ(sps->coded_height, 1088);
  EXPECT_EQ(sps->visible_width, 1920);
  EXPECT_EQ(sps->visible_height, 1080);
  EXPECT_EQ(sps->crop.bottom, 8U);
  EXPECT_EQ(sps->crop.left, 0U);
  EXPECT_TRUE(sps->frame_mbs_only);
  EXPECT_FALSE(sps->vui.video_signal_present);
  EXPECT_EQ(sps->vui.colour_primaries, 2);
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(1, 1));
  ASSERT_TRUE(sps->vui.frame_rate.has_value());
  EXPECT_EQ(*sps->vui.frame_rate, penumbra::Rational::Of(24, 1));
}

TEST(AvcTest, ParsesCroppingThatLeavesAnOddVisibleSize) {
  const absl::StatusOr<AvcConfig> config =
      ParseAvcC(FromHex(test_data::kAvcC1078));
  ASSERT_TRUE(config.ok());
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(config->sps[0]);
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->visible_width, 1918);
  EXPECT_EQ(sps->visible_height, 1078);
  EXPECT_EQ(sps->crop.right, 2U);
  EXPECT_EQ(sps->crop.bottom, 10U);
  const penumbra::VideoInfo info = ToVideoInfo(*sps);
  EXPECT_EQ(info.coded_width, 1920);
  EXPECT_EQ(info.coded_height, 1088);
  EXPECT_EQ(info.display_width, 1918);
  EXPECT_EQ(info.display_height, 1078);
}

TEST(AvcTest, SampleAspectRatioScalesTheDisplayWidth) {
  SpsSpec spec;
  spec.sar = {16, 15};
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(MakeSps(spec));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->coded_width, 720);
  EXPECT_EQ(sps->coded_height, 576);
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(16, 15));
  const penumbra::VideoInfo info = ToVideoInfo(*sps);
  EXPECT_EQ(info.display_width, 768);
  EXPECT_EQ(info.display_height, 576);
  EXPECT_EQ(info.sample_aspect_ratio, penumbra::Rational::Of(16, 15));
}

TEST(AvcTest, TableAspectRatiosAreResolved) {
  SpsSpec spec;
  spec.aspect_indicator = 14;
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(MakeSps(spec));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(4, 3));
  EXPECT_EQ(ToVideoInfo(*sps).display_width, 960);
}

TEST(AvcTest, CroppingProducesTheVisibleSizeForProgressiveAndInterlaced) {
  SpsSpec progressive;
  progressive.width_mbs_minus1 = 119;
  progressive.height_units_minus1 = 67;
  progressive.crop = {{0, 0, 0, 4}};
  absl::StatusOr<H264Sps> sps = ParseH264Sps(MakeSps(progressive));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->coded_height, 1088);
  EXPECT_EQ(sps->visible_width, 1920);
  EXPECT_EQ(sps->visible_height, 1080);

  SpsSpec interlaced;
  interlaced.width_mbs_minus1 = 119;
  interlaced.height_units_minus1 = 33;
  interlaced.frame_mbs_only = false;
  interlaced.crop = {{0, 0, 0, 2}};
  sps = ParseH264Sps(MakeSps(interlaced));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_FALSE(sps->frame_mbs_only);
  EXPECT_EQ(sps->coded_height, 1088);
  EXPECT_EQ(sps->visible_height, 1080);
}

TEST(AvcTest, ReadsColourAndTimingFromTheVui) {
  SpsSpec spec;
  spec.colour = true;
  spec.timing = true;
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(MakeSps(spec));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_TRUE(sps->vui.colour_description_present);
  EXPECT_TRUE(sps->vui.full_range);
  EXPECT_EQ(sps->vui.colour_primaries, 9);
  EXPECT_EQ(sps->vui.transfer_characteristics, 16);
  EXPECT_EQ(sps->vui.matrix_coefficients, 9);
  ASSERT_TRUE(sps->vui.frame_rate.has_value());
  EXPECT_EQ(*sps->vui.frame_rate, penumbra::Rational::Of(30000, 1001));
  const penumbra::VideoInfo info = ToVideoInfo(*sps);
  EXPECT_EQ(info.color.primaries, 9);
  EXPECT_EQ(info.color.transfer, 16);
  EXPECT_TRUE(info.color.full_range);
  EXPECT_EQ(info.frame_rate, penumbra::Rational::Of(30000, 1001));
}

TEST(AvcTest, HighProfileSpsWithoutChromaInfoDefaultsToFourTwoZero) {
  SpsSpec spec;
  const absl::StatusOr<H264Sps> sps = ParseH264Sps(MakeSps(spec));
  ASSERT_TRUE(sps.ok());
  EXPECT_EQ(sps->chroma_format_idc, 1);
  EXPECT_EQ(sps->bit_depth_luma, 8);
  spec.profile = 100;
  const absl::StatusOr<H264Sps> high = ParseH264Sps(MakeSps(spec));
  ASSERT_TRUE(high.ok()) << high.status();
  EXPECT_EQ(high->profile_idc, 100);
}

TEST(AvcTest, RejectsCroppingThatRemovesThePicture) {
  SpsSpec spec;
  spec.crop = {{0, 0, 0, 300}};
  EXPECT_EQ(ParseH264Sps(MakeSps(spec)).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(AvcTest, RejectsWrongNalTypeAndTruncatedSps) {
  const std::vector<std::uint8_t> nal = MakeSps({});
  std::vector<std::uint8_t> wrong = nal;
  wrong[0] = 0x68;
  EXPECT_EQ(ParseH264Sps(wrong).status().code(), absl::StatusCode::kDataLoss);
  for (std::size_t length = 0; length + 1 < nal.size(); ++length) {
    const absl::StatusOr<H264Sps> sps = ParseH264Sps(Bytes(nal).first(length));
    if (!sps.ok()) {
      EXPECT_EQ(sps.status().code(), absl::StatusCode::kDataLoss) << length;
    }
  }
}

TEST(AvcTest, TruncatedRecordsAreErrors) {
  const std::vector<std::uint8_t> record = FromHex(test_data::kAvcC1080);
  for (std::size_t length = 0; length < 44; ++length) {
    const absl::StatusOr<AvcConfig> config =
        ParseAvcC(Bytes(record).first(length));
    EXPECT_EQ(config.status().code(), absl::StatusCode::kDataLoss) << length;
  }
  std::vector<std::uint8_t> bad_version = record;
  bad_version[0] = 2;
  EXPECT_EQ(ParseAvcC(bad_version).status().code(),
            absl::StatusCode::kUnimplemented);
}

TEST(AvcTest, BuildsTheRecordFromInBandParameterSets) {
  const absl::StatusOr<AvcConfig> source =
      ParseAvcC(FromHex(test_data::kAvcC1080));
  ASSERT_TRUE(source.ok());
  std::vector<std::uint8_t> stream{0, 0, 0, 1, 0x09, 0xf0};
  const std::vector<std::uint8_t>& sps = source->sps.front();
  const std::vector<std::uint8_t>& pps = source->pps.front();
  for (const std::vector<std::uint8_t>& unit : {sps, pps, sps}) {
    stream.insert(stream.end(), {0, 0, 1});
    stream.insert(stream.end(), unit.begin(), unit.end());
  }
  stream.insert(stream.end(), {0, 0, 1, 0x65, 0x88, 0x84});
  const absl::StatusOr<AvcConfig> built = AvcConfigFromAnnexB(stream);
  ASSERT_TRUE(built.ok()) << built.status();
  EXPECT_EQ(built->sps.size(), 1U);
  EXPECT_EQ(built->pps.size(), 1U);
  EXPECT_EQ(FormatCodecString(ToCodec(*built)), "avc1.640028");
  const absl::StatusOr<std::vector<std::uint8_t>> record =
      SerializeAvcC(*built);
  ASSERT_TRUE(record.ok());
  EXPECT_THAT(*record, ElementsAreArray(FromHex(test_data::kAvcC1080)));
}

TEST(AvcTest, InBandConstructionNeedsAnSps) {
  EXPECT_EQ(AvcConfigFromAnnexB(FromHex("0000000168ce")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(AvcConfigFromAnnexB(FromHex("6742")).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(AvcTest, SerializeRejectsInvalidConfigurations) {
  AvcConfig config;
  config.nal_length_size = 3;
  EXPECT_EQ(SerializeAvcC(config).status().code(),
            absl::StatusCode::kInvalidArgument);
  config.nal_length_size = 4;
  config.sps.emplace_back();
  EXPECT_EQ(SerializeAvcC(config).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
