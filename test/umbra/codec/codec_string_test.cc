#include "umbra/codec/codec_string.h"

#include <array>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::codec {
namespace {

constexpr std::array<std::string_view, 24> kCanonical{
    "avc1.640020",
    "avc1.64001f",
    "avc3.42e01e",
    "hvc1.1.6.L93.B0",
    "hev1.2.4.L153.B0",
    "hvc1.1.6.L120.90",
    "hvc1.2.4.L123",
    "hvc1.A4.10.H186.B0.12",
    "vp09.02.10.10.01.09.16.09.01",
    "vp09.00.41.08",
    "vp09.00.21.08.01.02.02.02.00",
    "av01.0.04M.10.0.112.09.16.09.0",
    "av01.0.01M.08",
    "av01.1.13H.12.0.000.01.01.01.1",
    "mp4a.40.2",
    "mp4a.40.5",
    "mp4a.40.34",
    "mp4a.6B",
    "opus",
    "fLaC",
    "ac-3",
    "ec-3",
    "dvh1.05.06",
    "dvhe.08.07",
};

TEST(CodecStringTest, CanonicalStringsRoundTrip) {
  for (const std::string_view text : kCanonical) {
    const absl::StatusOr<CodecDescription> parsed = ParseCodecString(text);
    ASSERT_TRUE(parsed.ok()) << text << ": " << parsed.status();
    EXPECT_EQ(FormatCodecString(*parsed), text);
    const absl::StatusOr<CodecDescription> again =
        ParseCodecString(FormatCodecString(*parsed));
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(*again, *parsed);
  }
}

TEST(CodecStringTest, AvcFieldsFollowTheProfileConstraintLevelOrder) {
  const absl::StatusOr<CodecDescription> parsed =
      ParseCodecString("avc1.640020");
  ASSERT_TRUE(parsed.ok());
  const auto& avc = std::get<AvcCodec>(*parsed);
  EXPECT_EQ(avc.sample_entry, "avc1");
  EXPECT_EQ(avc.profile_idc, 0x64);
  EXPECT_EQ(avc.constraint_flags, 0);
  EXPECT_EQ(avc.level_idc, 0x20);
  EXPECT_EQ(CodecIdOf(*parsed), penumbra::CodecId::kH264);
  EXPECT_EQ(ParseCodecString("AVC1.640020").status().code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(*ParseCodecString("avc1.64001F"), *ParseCodecString("avc1.64001f"));
}

TEST(CodecStringTest, HevcMatchesAnnexEOfIso14496Part15) {
  const absl::StatusOr<CodecDescription> parsed =
      ParseCodecString("hvc1.2.4.L153.B0");
  ASSERT_TRUE(parsed.ok());
  const auto& hevc = std::get<HevcCodec>(*parsed);
  EXPECT_EQ(hevc.profile_space, 0);
  EXPECT_EQ(hevc.profile_idc, 2);
  EXPECT_EQ(hevc.compatibility_flags, 0x20000000U);
  EXPECT_FALSE(hevc.high_tier);
  EXPECT_EQ(hevc.level_idc, 153);
  EXPECT_EQ(hevc.constraint_flags[0], 0xb0);
  EXPECT_EQ(hevc.constraint_flags[1], 0);

  const auto main = std::get<HevcCodec>(*ParseCodecString("hvc1.1.6.L93.B0"));
  EXPECT_EQ(main.compatibility_flags, 0x60000000U);

  const auto spaced = std::get<HevcCodec>(*ParseCodecString("hvc1.B1.6.H93"));
  EXPECT_EQ(spaced.profile_space, 2);
  EXPECT_TRUE(spaced.high_tier);
}

TEST(CodecStringTest, HevcOmitsTrailingZeroConstraintBytes) {
  HevcCodec codec;
  codec.profile_idc = 1;
  codec.compatibility_flags = 0x60000000U;
  codec.level_idc = 93;
  codec.constraint_flags = {0xb0, 0, 0x01, 0, 0, 0};
  EXPECT_EQ(FormatCodecString(codec), "hvc1.1.6.L93.B0.0.1");
  codec.constraint_flags = {};
  EXPECT_EQ(FormatCodecString(codec), "hvc1.1.6.L93");
}

TEST(CodecStringTest, Vp9AcceptsOptionalFieldsAllOrNone) {
  const auto short_form =
      std::get<Vp9Codec>(*ParseCodecString("vp09.00.41.08"));
  EXPECT_FALSE(short_form.has_optional_fields);
  EXPECT_EQ(short_form.level, 41);
  EXPECT_EQ(short_form.chroma_subsampling, 1);
  const auto hdr =
      std::get<Vp9Codec>(*ParseCodecString("vp09.02.10.10.01.09.16.09.01"));
  EXPECT_EQ(hdr.profile, 2);
  EXPECT_EQ(hdr.bit_depth, 10);
  EXPECT_EQ(hdr.colour_primaries, 9);
  EXPECT_EQ(hdr.transfer_characteristics, 16);
  EXPECT_EQ(hdr.matrix_coefficients, 9);
  EXPECT_TRUE(hdr.full_range);
  EXPECT_FALSE(ParseCodecString("vp09.00.41.08.01").ok());
  EXPECT_FALSE(ParseCodecString("vp09.00.41").ok());
  EXPECT_FALSE(ParseCodecString("vp09.0.41.08").ok());
  EXPECT_FALSE(ParseCodecString("vp09.00.41.09").ok());
  EXPECT_FALSE(ParseCodecString("vp09.04.41.08").ok());
}

TEST(CodecStringTest, Av1MatchesTheIsobmffBinding) {
  const auto hdr =
      std::get<Av1Codec>(*ParseCodecString("av01.0.04M.10.0.112.09.16.09.0"));
  EXPECT_EQ(hdr.profile, 0);
  EXPECT_EQ(hdr.level, 4);
  EXPECT_FALSE(hdr.high_tier);
  EXPECT_EQ(hdr.bit_depth, 10);
  EXPECT_FALSE(hdr.monochrome);
  EXPECT_TRUE(hdr.subsampling_x);
  EXPECT_TRUE(hdr.subsampling_y);
  EXPECT_EQ(hdr.chroma_sample_position, 2);
  EXPECT_EQ(hdr.colour_primaries, 9);
  EXPECT_EQ(hdr.transfer_characteristics, 16);
  EXPECT_EQ(hdr.matrix_coefficients, 9);
  EXPECT_FALSE(hdr.full_range);

  const auto defaults = std::get<Av1Codec>(*ParseCodecString("av01.0.01M.08"));
  EXPECT_EQ(defaults.level, 1);
  EXPECT_EQ(defaults.colour_primaries, 1);
  EXPECT_EQ(defaults.chroma_sample_position, 0);
}

TEST(CodecStringTest, Av1OmitsTheAllDefaultTail) {
  const std::string_view full = "av01.0.01M.08.0.110.01.01.01.0";
  const absl::StatusOr<CodecDescription> parsed = ParseCodecString(full);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(FormatCodecString(*parsed), "av01.0.01M.08");
}

TEST(CodecStringTest, Av1RejectsMalformedFields) {
  for (const std::string_view text :
       {"av01", "av01.0.01M", "av01.0.01M.08.0", "av01.3.01M.08",
        "av01.0.01X.08", "av01.0.1M.08", "av01.0.01M.09",
        "av01.0.01M.08.2.110.01.01.01.0", "av01.0.01M.08.0.210.01.01.01.0",
        "av01.0.01M.08.0.001.01.01.01.0", "av01.0.01M.08.0.110.01.01.01.2"}) {
    EXPECT_EQ(ParseCodecString(text).status().code(),
              absl::StatusCode::kInvalidArgument)
        << text;
  }
}

TEST(CodecStringTest, AudioStringsMapToCodecIds) {
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.40.2")), penumbra::CodecId::kAac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.40.5")), penumbra::CodecId::kAac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.40.29")),
            penumbra::CodecId::kAac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.40.34")),
            penumbra::CodecId::kMp3);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.6B")), penumbra::CodecId::kMp3);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.69")), penumbra::CodecId::kMp3);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.67")), penumbra::CodecId::kAac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("mp4a.A5")),
            penumbra::CodecId::kUnknown);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("opus")), penumbra::CodecId::kOpus);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("Opus")), penumbra::CodecId::kOpus);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("flac")), penumbra::CodecId::kFlac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("fLaC")), penumbra::CodecId::kFlac);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("ac-3")), penumbra::CodecId::kAc3);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("ec-3")), penumbra::CodecId::kEac3);
}

TEST(CodecStringTest, Mp4aWithoutObjectTypeKeepsOnlyTheIndication) {
  const auto bare = std::get<Mp4aCodec>(*ParseCodecString("mp4a.40"));
  EXPECT_EQ(bare.object_type_indication, 0x40);
  EXPECT_FALSE(bare.audio_object_type.has_value());
  EXPECT_EQ(FormatCodecString(bare), "mp4a.40");
  EXPECT_FALSE(ParseCodecString("mp4a.4").ok());
  EXPECT_FALSE(ParseCodecString("mp4a.40.2.1").ok());
  EXPECT_FALSE(ParseCodecString("mp4a.40.x").ok());
}

TEST(CodecStringTest, DolbyVisionMapsToItsBaseLayer) {
  EXPECT_EQ(CodecIdOf(*ParseCodecString("dvh1.05.06")),
            penumbra::CodecId::kHevc);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("dvhe.08.07")),
            penumbra::CodecId::kHevc);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("dav1.10.09")),
            penumbra::CodecId::kAv1);
  EXPECT_EQ(CodecIdOf(*ParseCodecString("dvav.09.03")),
            penumbra::CodecId::kH264);
  EXPECT_FALSE(ParseCodecString("dvh1.5.06").ok());
}

TEST(CodecStringTest, RejectsMalformedStrings) {
  for (const std::string_view text :
       {"", ".", "avc1", "avc1.", "avc1.64002", "avc1.64002g", "avc1.640020.1",
        "hvc1", "hvc1.1.6", "hvc1.1.6.X93", "hvc1.32.6.L93",
        "hvc1.1.6.L93.B0.0.0.0.0.0.0", "hvc1.1.123456789.L93", "opus.1",
        "ac-3.1", "vp09"}) {
    EXPECT_FALSE(ParseCodecString(text).ok()) << text;
  }
  EXPECT_EQ(ParseCodecString("theora").status().code(),
            absl::StatusCode::kUnimplemented);
}

TEST(CodecStringTest, ParsesHlsAndDashCodecLists) {
  const absl::StatusOr<std::vector<CodecDescription>> list =
      ParseCodecList("\"avc1.640020, mp4a.40.2\"");
  ASSERT_TRUE(list.ok()) << list.status();
  ASSERT_EQ(list->size(), 2U);
  EXPECT_EQ(FormatCodecString((*list)[0]), "avc1.640020");
  EXPECT_EQ(FormatCodecString((*list)[1]), "mp4a.40.2");
  EXPECT_TRUE(ParseCodecList("")->empty());
  EXPECT_FALSE(ParseCodecList("avc1.640020,,ac-3").ok());
  EXPECT_FALSE(ParseCodecList("avc1.640020,bogus").ok());
}

}
}
