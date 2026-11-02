#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fixtures.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/stream_info.h"
#include "test_util.h"
#include "umbra/codec/av1.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/vp9.h"

namespace umbra::codec {
namespace {

using test_data::BitWriter;
using test_data::FromHex;
using ::testing::ElementsAreArray;

TEST(Vp9Test, ParsesTheVpcCRecordOfALibvpxStream) {
  const absl::StatusOr<Vp9Config> config = ParseVpcC(FromHex(test_data::kVpcC));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->profile, 0);
  EXPECT_EQ(config->level, 21);
  EXPECT_EQ(config->bit_depth, 8);
  EXPECT_EQ(config->chroma_subsampling, 1);
  EXPECT_FALSE(config->full_range);
  EXPECT_EQ(config->colour_primaries, 2);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)),
            "vp09.00.21.08.01.02.02.02.00");
}

TEST(Vp9Test, ProducesTheStringOfTheBindingExample) {
  Vp9Config config;
  config.profile = 2;
  config.level = 10;
  config.bit_depth = 10;
  config.chroma_subsampling = 1;
  config.colour_primaries = 9;
  config.transfer_characteristics = 16;
  config.matrix_coefficients = 9;
  config.full_range = true;
  EXPECT_EQ(FormatCodecString(ToCodec(config)), "vp09.02.10.10.01.09.16.09.01");
  const penumbra::ColorSpace color = ToColorSpace(config);
  EXPECT_EQ(color.primaries, 9);
  EXPECT_EQ(color.transfer, 16);
  EXPECT_TRUE(color.full_range);
}

TEST(Vp9Test, RoundTripsThroughTheRecord) {
  Vp9Config config;
  config.profile = 3;
  config.level = 62;
  config.bit_depth = 12;
  config.chroma_subsampling = 3;
  config.full_range = true;
  const absl::StatusOr<std::vector<std::uint8_t>> record =
      SerializeVpcC(config);
  ASSERT_TRUE(record.ok());
  const absl::StatusOr<Vp9Config> parsed = ParseVpcC(*record);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(*parsed, config);
  const std::vector<std::uint8_t> sample = FromHex(test_data::kVpcC);
  const absl::StatusOr<Vp9Config> original = ParseVpcC(sample);
  ASSERT_TRUE(original.ok());
  EXPECT_THAT(*SerializeVpcC(*original), ElementsAreArray(sample));
}

TEST(Vp9Test, RejectsBadRecords) {
  const std::vector<std::uint8_t> record = FromHex(test_data::kVpcC);
  for (std::size_t length = 0; length < record.size(); ++length) {
    EXPECT_EQ(ParseVpcC(Bytes(record).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
  std::vector<std::uint8_t> old_version = record;
  old_version[0] = 0;
  EXPECT_EQ(ParseVpcC(old_version).status().code(),
            absl::StatusCode::kUnimplemented);
  std::vector<std::uint8_t> bad_depth = record;
  bad_depth[6] = 0x92;
  EXPECT_EQ(ParseVpcC(bad_depth).status().code(), absl::StatusCode::kDataLoss);
  Vp9Config invalid;
  invalid.bit_depth = 9;
  EXPECT_EQ(SerializeVpcC(invalid).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(Av1Test, ParsesTheAv1CRecordOfALibaomStream) {
  const absl::StatusOr<Av1Config> config = ParseAv1C(FromHex(test_data::kAv1C));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->profile, 0);
  EXPECT_EQ(config->level, 1);
  EXPECT_FALSE(config->high_tier);
  EXPECT_EQ(BitDepthOf(*config), 8);
  EXPECT_TRUE(config->subsampling_x);
  EXPECT_TRUE(config->subsampling_y);
  EXPECT_FALSE(config->initial_presentation_delay_minus_one.has_value());
  EXPECT_EQ(config->config_obus.size(), 13U);
  ASSERT_TRUE(config->sequence_header.has_value());
  EXPECT_EQ(config->sequence_header->max_frame_width, 640U);
  EXPECT_EQ(config->sequence_header->max_frame_height, 360U);
  EXPECT_EQ(config->sequence_header->bit_depth, 8);
  EXPECT_FALSE(config->sequence_header->colour_description_present);
}

TEST(Av1Test, GeneratesTheCodecStrings) {
  const absl::StatusOr<Av1Config> eight = ParseAv1C(FromHex(test_data::kAv1C));
  const absl::StatusOr<Av1Config> ten = ParseAv1C(FromHex(test_data::kAv1C10));
  ASSERT_TRUE(eight.ok());
  ASSERT_TRUE(ten.ok());
  EXPECT_EQ(FormatCodecString(ToCodec(*eight)), "av01.0.01M.08");
  EXPECT_EQ(FormatCodecString(ToCodec(*ten)), "av01.0.01M.10");
  EXPECT_EQ(ten->sequence_header->bit_depth, 10);
}

TEST(Av1Test, SerializesTheRecordBackToTheSameBytes) {
  for (const auto record : {test_data::kAv1C, test_data::kAv1C10}) {
    const std::vector<std::uint8_t> bytes = FromHex(record);
    const absl::StatusOr<Av1Config> config = ParseAv1C(bytes);
    ASSERT_TRUE(config.ok());
    const absl::StatusOr<std::vector<std::uint8_t>> again =
        SerializeAv1C(*config);
    ASSERT_TRUE(again.ok());
    EXPECT_THAT(*again, ElementsAreArray(bytes));
  }
}

TEST(Av1Test, BuildsTheRecordFromASequenceHeaderObu) {
  const absl::StatusOr<Av1Config> source =
      ParseAv1C(FromHex(test_data::kAv1C10));
  ASSERT_TRUE(source.ok());
  const absl::StatusOr<Av1Config> built =
      Av1ConfigFromSequenceHeaderObu(source->config_obus);
  ASSERT_TRUE(built.ok()) << built.status();
  EXPECT_THAT(*SerializeAv1C(*built),
              ElementsAreArray(FromHex(test_data::kAv1C10)));
  EXPECT_EQ(Av1ConfigFromSequenceHeaderObu(FromHex("0a0b00")).status().code(),
            absl::StatusCode::kDataLoss);
}

std::vector<std::uint8_t> MakeSequenceHeaderObu(const BitWriter& payload) {
  BitWriter copy = payload;
  std::vector<std::uint8_t> bytes = copy.Rbsp();
  std::vector<std::uint8_t> obu{0x0a, static_cast<std::uint8_t>(bytes.size())};
  obu.insert(obu.end(), bytes.begin(), bytes.end());
  return obu;
}

TEST(Av1Test, ParsesAnHdrSequenceHeaderWithTimingAndOperatingPoints) {
  BitWriter bits;
  bits.Put(0, 3).Flag(false).Flag(false);
  bits.Flag(true).Put(1001, 32).Put(24000, 32).Flag(true).Ue(0);
  bits.Flag(true).Put(10, 5).Put(32, 32).Put(9, 5).Put(5, 5);
  bits.Flag(true);
  bits.Put(1, 5);
  bits.Put(0x100, 12).Put(8, 5).Flag(true).Flag(true);
  bits.Put(0x3ff, 11).Put(0x3ff, 11).Flag(false).Flag(true).Put(7, 4);
  bits.Put(0x101, 12).Put(4, 5).Flag(false).Flag(false);
  bits.Put(11, 4).Put(11, 4).Put(3839, 12).Put(2159, 12);
  bits.Flag(true).Put(5, 4).Put(2, 3);
  bits.Put(0, 3);
  bits.Put(0, 4).Flag(true).Put(0, 2).Flag(false).Flag(true).Flag(true);
  bits.Put(3, 3);
  bits.Put(0, 3);
  bits.Flag(true).Flag(false).Flag(true);
  bits.Put(9, 8).Put(16, 8).Put(9, 8).Flag(false).Put(2, 2);
  const absl::StatusOr<Av1SequenceHeader> header =
      ParseAv1SequenceHeaderObu(MakeSequenceHeaderObu(bits));
  ASSERT_TRUE(header.ok()) << header.status();
  EXPECT_EQ(header->profile, 0);
  EXPECT_EQ(header->level, 8);
  EXPECT_TRUE(header->high_tier);
  EXPECT_EQ(header->max_frame_width, 3840U);
  EXPECT_EQ(header->max_frame_height, 2160U);
  EXPECT_EQ(header->bit_depth, 10);
  EXPECT_TRUE(header->colour_description_present);
  EXPECT_EQ(header->colour_primaries, 9);
  EXPECT_EQ(header->transfer_characteristics, 16);
  EXPECT_EQ(header->matrix_coefficients, 9);
  EXPECT_FALSE(header->full_range);
  EXPECT_TRUE(header->subsampling_x);
  EXPECT_TRUE(header->subsampling_y);
  EXPECT_EQ(header->chroma_sample_position, 2);
  EXPECT_EQ(FormatCodecString(ToCodec(*header)),
            "av01.0.08H.10.0.112.09.16.09.0");
}

TEST(Av1Test, ParsesAReducedStillPictureHeader) {
  BitWriter bits;
  bits.Put(1, 3).Flag(true).Flag(true).Put(5, 5);
  bits.Put(7, 4).Put(7, 4).Put(255, 8).Put(255, 8);
  bits.Put(0, 3);
  bits.Put(0, 3);
  bits.Flag(false).Flag(false).Flag(false);
  const absl::StatusOr<Av1SequenceHeader> header =
      ParseAv1SequenceHeaderObu(MakeSequenceHeaderObu(bits));
  ASSERT_TRUE(header.ok()) << header.status();
  EXPECT_EQ(header->profile, 1);
  EXPECT_TRUE(header->still_picture);
  EXPECT_TRUE(header->reduced_still_picture_header);
  EXPECT_EQ(header->level, 5);
  EXPECT_EQ(header->max_frame_width, 256U);
  EXPECT_EQ(header->bit_depth, 8);
  EXPECT_FALSE(header->subsampling_x);
  EXPECT_FALSE(header->subsampling_y);
}

TEST(Av1Test, ParsesMonochromeAndTheSrgbIdentityShortcut) {
  BitWriter mono;
  mono.Put(0, 3).Flag(false).Flag(true).Put(4, 5);
  mono.Put(7, 4).Put(7, 4).Put(255, 8).Put(255, 8);
  mono.Put(0, 3).Put(0, 3);
  mono.Flag(false).Flag(true).Flag(false).Flag(true);
  absl::StatusOr<Av1SequenceHeader> header =
      ParseAv1SequenceHeaderObu(MakeSequenceHeaderObu(mono));
  ASSERT_TRUE(header.ok()) << header.status();
  EXPECT_TRUE(header->monochrome);
  EXPECT_TRUE(header->full_range);

  BitWriter rgb;
  rgb.Put(1, 3).Flag(false).Flag(true).Put(4, 5);
  rgb.Put(7, 4).Put(7, 4).Put(255, 8).Put(255, 8);
  rgb.Put(0, 3).Put(0, 3);
  rgb.Flag(false).Flag(true).Put(1, 8).Put(13, 8).Put(0, 8);
  header = ParseAv1SequenceHeaderObu(MakeSequenceHeaderObu(rgb));
  ASSERT_TRUE(header.ok()) << header.status();
  EXPECT_TRUE(header->full_range);
  EXPECT_FALSE(header->subsampling_x);
  EXPECT_FALSE(header->subsampling_y);
}

TEST(Av1Test, FindsTheSequenceHeaderAmongOtherObus) {
  const std::vector<std::uint8_t> config =
      ParseAv1C(FromHex(test_data::kAv1C))->config_obus;
  std::vector<std::uint8_t> obus = FromHex("1200");
  obus.insert(obus.end(), config.begin(), config.end());
  const absl::StatusOr<std::optional<Av1SequenceHeader>> found =
      FindAv1SequenceHeader(obus);
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_TRUE(found->has_value());
  EXPECT_FALSE(FindAv1SequenceHeader(FromHex("1200"))->has_value());
  EXPECT_FALSE(FindAv1SequenceHeader({})->has_value());
}

TEST(Av1Test, RejectsMalformedRecords) {
  const std::vector<std::uint8_t> record = FromHex(test_data::kAv1C);
  for (std::size_t length = 0; length < 4; ++length) {
    EXPECT_EQ(ParseAv1C(Bytes(record).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
  for (std::size_t length = 4; length < record.size(); ++length) {
    const absl::StatusOr<Av1Config> config =
        ParseAv1C(Bytes(record).first(length));
    if (!config.ok()) {
      EXPECT_EQ(config.status().code(), absl::StatusCode::kDataLoss) << length;
    }
  }
  std::vector<std::uint8_t> no_marker = record;
  no_marker[0] = 0x01;
  EXPECT_EQ(ParseAv1C(no_marker).status().code(), absl::StatusCode::kDataLoss);
  std::vector<std::uint8_t> version = record;
  version[0] = 0x82;
  EXPECT_EQ(ParseAv1C(version).status().code(),
            absl::StatusCode::kUnimplemented);
  Av1Config invalid;
  invalid.profile = 3;
  EXPECT_EQ(SerializeAv1C(invalid).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
