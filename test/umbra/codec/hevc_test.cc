#include "umbra/codec/hevc.h"

#include <cstddef>
#include <cstdint>
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

const HevcNalArray* FindArray(const HevcConfig& config, std::uint8_t type) {
  for (const HevcNalArray& array : config.arrays) {
    if (array.nal_unit_type == type) {
      return &array;
    }
  }
  return nullptr;
}

void WritePtl(BitWriter& bits, int sub_layers_minus1) {
  bits.Put(0, 2).Flag(false).Put(1, 5).Put(0x60000000U, 32);
  bits.Put(0x900000000000ULL, 48).Put(93, 8);
  for (int i = 0; i < sub_layers_minus1; ++i) {
    bits.Flag(i == 0).Flag(i == 0);
  }
  if (sub_layers_minus1 > 0) {
    for (int i = sub_layers_minus1; i < 8; ++i) {
      bits.Put(0, 2);
    }
  }
  for (int i = 0; i < sub_layers_minus1; ++i) {
    if (i == 0) {
      bits.Put(0, 44).Put(0, 44).Put(90, 8);
    }
  }
}

struct SpsSpec {
  int sub_layers_minus1 = 0;
  std::uint32_t width = 1920;
  std::uint32_t height = 1088;
  bool window = true;
  bool scaling = true;
  bool long_term = true;
  bool vui = true;
  bool sar = true;
};

void WriteScalingListData(BitWriter& bits) {
  bits.Flag(true);
  for (int size_id = 0; size_id < 4; ++size_id) {
    for (int matrix = 0; matrix < 6; matrix += size_id == 3 ? 3 : 1) {
      if (matrix == 1) {
        bits.Flag(false).Ue(1);
        continue;
      }
      bits.Flag(true);
      if (size_id > 1) {
        bits.Se(3);
      }
      const int coefficients = size_id == 0 ? 16 : 64;
      for (int i = 0; i < coefficients; ++i) {
        bits.Se(i == 0 ? -2 : 1);
      }
    }
  }
}

void WriteReferenceSets(BitWriter& bits) {
  bits.Ue(3);
  bits.Ue(2).Ue(0).Ue(0).Flag(true).Ue(0).Flag(true);
  bits.Flag(true).Flag(false).Ue(0);
  bits.Flag(true).Flag(false).Flag(true).Flag(false).Flag(false);
  bits.Flag(false).Ue(1).Ue(1).Ue(0).Flag(true).Ue(0).Flag(true);
}

void WriteVui(BitWriter& bits, bool sar) {
  bits.Flag(sar);
  if (sar) {
    bits.Put(255, 8).Put(32, 16).Put(27, 16);
  }
  bits.Flag(false);
  bits.Flag(true).Put(5, 3).Flag(false).Flag(true);
  bits.Put(9, 8).Put(16, 8).Put(9, 8);
  bits.Flag(false);
  bits.Put(0, 3).Flag(false);
  bits.Flag(true).Put(1001, 32).Put(24000, 32);
}

void WriteCodingTools(BitWriter& bits, const SpsSpec& spec) {
  bits.Ue(2).Ue(2).Ue(4).Flag(true);
  for (int i = 0; i <= spec.sub_layers_minus1; ++i) {
    bits.Ue(4).Ue(2).Ue(0);
  }
  bits.Ue(0).Ue(3).Ue(0).Ue(3).Ue(1).Ue(1);
  bits.Flag(spec.scaling);
  if (spec.scaling) {
    WriteScalingListData(bits);
  }
  bits.Flag(true).Flag(true);
  bits.Flag(true).Put(7, 4).Put(7, 4).Ue(0).Ue(2).Flag(true);
}

std::vector<std::uint8_t> MakeSps(const SpsSpec& spec) {
  BitWriter bits;
  bits.Put(0, 4).Put(spec.sub_layers_minus1, 3).Flag(true);
  WritePtl(bits, spec.sub_layers_minus1);
  bits.Ue(0).Ue(1).Ue(spec.width).Ue(spec.height);
  bits.Flag(spec.window);
  if (spec.window) {
    bits.Ue(0).Ue(0).Ue(0).Ue(4);
  }
  WriteCodingTools(bits, spec);
  WriteReferenceSets(bits);
  bits.Flag(spec.long_term);
  if (spec.long_term) {
    bits.Ue(2).Put(5, 8).Flag(true).Put(9, 8).Flag(false);
  }
  bits.Flag(true).Flag(true);
  bits.Flag(spec.vui);
  if (spec.vui) {
    WriteVui(bits, spec.sar);
  }
  std::vector<std::uint8_t> nal{0x42, 0x01};
  const std::vector<std::uint8_t> payload =
      WithEmulationPrevention(bits.Rbsp());
  nal.insert(nal.end(), payload.begin(), payload.end());
  return nal;
}

TEST(HevcTest, ParsesTheHvcCRecordOfAnX265Stream) {
  const absl::StatusOr<HevcConfig> config =
      ParseHvcC(FromHex(test_data::kHvcC1080Main));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->profile.profile_space, 0);
  EXPECT_FALSE(config->profile.high_tier);
  EXPECT_EQ(config->profile.profile_idc, 1);
  EXPECT_EQ(config->profile.compatibility_flags, 0x60000000U);
  EXPECT_EQ(config->profile.constraint_flags[0], 0x90);
  EXPECT_EQ(config->profile.level_idc, 120);
  EXPECT_EQ(config->chroma_format, 1);
  EXPECT_EQ(config->bit_depth_luma, 8);
  EXPECT_EQ(config->nal_length_size, 4);
  ASSERT_EQ(config->arrays.size(), 3U);
  ASSERT_NE(FindArray(*config, kHevcVpsNalType), nullptr);
  ASSERT_NE(FindArray(*config, kHevcSpsNalType), nullptr);
  ASSERT_NE(FindArray(*config, kHevcPpsNalType), nullptr);
  EXPECT_TRUE(FindArray(*config, kHevcSpsNalType)->array_completeness);
}

TEST(HevcTest, GeneratesTheCodecStringsOfTheMainAndMain10Profiles) {
  const absl::StatusOr<HevcConfig> main =
      ParseHvcC(FromHex(test_data::kHvcC1080Main));
  const absl::StatusOr<HevcConfig> main10 =
      ParseHvcC(FromHex(test_data::kHvcC1080Main10));
  ASSERT_TRUE(main.ok());
  ASSERT_TRUE(main10.ok());
  EXPECT_EQ(FormatCodecString(ToCodec(*main)), "hvc1.1.6.L120.90");
  EXPECT_EQ(FormatCodecString(ToCodec(*main10)), "hvc1.2.4.L120.90");
  EXPECT_EQ(FormatCodecString(ToCodec(*main10, "hev1")), "hev1.2.4.L120.90");
}

TEST(HevcTest, SerializesTheRecordBackToTheSameBytes) {
  for (const auto record :
       {test_data::kHvcC1080Main, test_data::kHvcC1080Main10}) {
    const std::vector<std::uint8_t> bytes = FromHex(record);
    const absl::StatusOr<HevcConfig> config = ParseHvcC(bytes);
    ASSERT_TRUE(config.ok());
    const absl::StatusOr<std::vector<std::uint8_t>> again =
        SerializeHvcC(*config);
    ASSERT_TRUE(again.ok()) << again.status();
    EXPECT_THAT(*again, ElementsAreArray(bytes));
  }
}

TEST(HevcTest, ParsesTheSpsOfBothStreams) {
  const absl::StatusOr<HevcConfig> main10 =
      ParseHvcC(FromHex(test_data::kHvcC1080Main10));
  ASSERT_TRUE(main10.ok());
  const absl::StatusOr<HevcSps> sps =
      ParseHevcSps(FindArray(*main10, kHevcSpsNalType)->units[0]);
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->profile.profile_idc, 2);
  EXPECT_EQ(sps->profile.level_idc, 120);
  EXPECT_EQ(sps->chroma_format_idc, 1);
  EXPECT_EQ(sps->bit_depth_luma, 10);
  EXPECT_EQ(sps->bit_depth_chroma, 10);
  EXPECT_EQ(sps->coded_width, 1920);
  EXPECT_EQ(sps->coded_height, 1080);
  EXPECT_EQ(sps->visible_width, 1920);
  EXPECT_EQ(sps->visible_height, 1080);
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(1, 1));
}

TEST(HevcTest, ParsesTheVps) {
  const absl::StatusOr<HevcConfig> main =
      ParseHvcC(FromHex(test_data::kHvcC1080Main));
  ASSERT_TRUE(main.ok());
  const absl::StatusOr<HevcVps> vps =
      ParseHevcVps(FindArray(*main, kHevcVpsNalType)->units[0]);
  ASSERT_TRUE(vps.ok()) << vps.status();
  EXPECT_EQ(vps->max_sub_layers, 1);
  EXPECT_EQ(vps->profile.profile_idc, 1);
  EXPECT_EQ(vps->profile.level_idc, 120);
}

TEST(HevcTest, ParsesEveryOptionalSpsSectionThatPrecedesTheVui) {
  const absl::StatusOr<HevcSps> sps = ParseHevcSps(MakeSps({}));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->profile.profile_idc, 1);
  EXPECT_EQ(sps->profile.level_idc, 93);
  EXPECT_EQ(sps->coded_width, 1920);
  EXPECT_EQ(sps->coded_height, 1088);
  EXPECT_EQ(sps->visible_height, 1080);
  EXPECT_EQ(sps->crop.bottom, 8U);
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(32, 27));
  EXPECT_EQ(sps->vui.colour_primaries, 9);
  EXPECT_EQ(sps->vui.transfer_characteristics, 16);
  EXPECT_EQ(sps->vui.matrix_coefficients, 9);
  EXPECT_FALSE(sps->vui.full_range);
  ASSERT_TRUE(sps->vui.frame_rate.has_value());
  EXPECT_EQ(*sps->vui.frame_rate, penumbra::Rational::Of(24000, 1001));
}

TEST(HevcTest, SampleAspectRatioScalesTheDisplayWidth) {
  const absl::StatusOr<HevcSps> sps = ParseHevcSps(MakeSps({}));
  ASSERT_TRUE(sps.ok());
  const penumbra::VideoInfo info = ToVideoInfo(*sps);
  EXPECT_EQ(info.coded_width, 1920);
  EXPECT_EQ(info.coded_height, 1088);
  EXPECT_EQ(info.display_width, 2276);
  EXPECT_EQ(info.display_height, 1080);
  EXPECT_EQ(info.color.primaries, 9);
}

TEST(HevcTest, ParsesSubLayersAndOmittedSections) {
  SpsSpec spec;
  spec.sub_layers_minus1 = 2;
  spec.window = false;
  spec.scaling = false;
  spec.long_term = false;
  spec.sar = false;
  const absl::StatusOr<HevcSps> sps = ParseHevcSps(MakeSps(spec));
  ASSERT_TRUE(sps.ok()) << sps.status();
  EXPECT_EQ(sps->max_sub_layers, 3);
  EXPECT_EQ(sps->visible_height, 1088);
  EXPECT_EQ(sps->vui.sample_aspect_ratio, penumbra::Rational::Of(1, 1));
  EXPECT_EQ(sps->vui.colour_primaries, 9);
}

TEST(HevcTest, RejectsWrongNalTypesAndTruncation) {
  const std::vector<std::uint8_t> nal = MakeSps({});
  std::vector<std::uint8_t> wrong = nal;
  wrong[0] = 0x44;
  EXPECT_EQ(ParseHevcSps(wrong).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseHevcVps(nal).status().code(), absl::StatusCode::kDataLoss);
  for (std::size_t length = 0; length + 1 < nal.size(); ++length) {
    const absl::StatusOr<HevcSps> sps = ParseHevcSps(Bytes(nal).first(length));
    if (!sps.ok()) {
      EXPECT_EQ(sps.status().code(), absl::StatusCode::kDataLoss) << length;
    }
  }
}

TEST(HevcTest, TruncatedRecordsAreErrors) {
  const std::vector<std::uint8_t> record = FromHex(test_data::kHvcC1080Main);
  for (std::size_t length = 0; length < record.size(); ++length) {
    EXPECT_EQ(ParseHvcC(Bytes(record).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
  std::vector<std::uint8_t> bad_version = record;
  bad_version[0] = 0;
  EXPECT_EQ(ParseHvcC(bad_version).status().code(),
            absl::StatusCode::kUnimplemented);
}

TEST(HevcTest, BuildsTheRecordFromInBandParameterSets) {
  const absl::StatusOr<HevcConfig> source =
      ParseHvcC(FromHex(test_data::kHvcC1080Main));
  ASSERT_TRUE(source.ok());
  std::vector<std::uint8_t> stream;
  for (const std::uint8_t type :
       {kHevcVpsNalType, kHevcSpsNalType, kHevcPpsNalType}) {
    stream.insert(stream.end(), {0, 0, 0, 1});
    const std::vector<std::uint8_t>& unit = FindArray(*source, type)->units[0];
    stream.insert(stream.end(), unit.begin(), unit.end());
  }
  stream.insert(stream.end(), {0, 0, 1, 0x26, 0x01, 0xaf});
  const absl::StatusOr<HevcConfig> built = HevcConfigFromAnnexB(stream);
  ASSERT_TRUE(built.ok()) << built.status();
  EXPECT_EQ(FormatCodecString(ToCodec(*built)), "hvc1.1.6.L120.90");
  EXPECT_EQ(built->arrays.size(), 3U);
  EXPECT_EQ(HevcConfigFromAnnexB(FromHex("0000000140010c")).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(HevcTest, SerializeRejectsOutOfRangeFields) {
  HevcConfig config;
  config.profile.profile_idc = 32;
  EXPECT_EQ(SerializeHvcC(config).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
