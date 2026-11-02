#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fixtures.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/stream_info.h"
#include "test_util.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/ac3.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/flac.h"
#include "umbra/codec/opus.h"

namespace umbra::codec {
namespace {

using test_data::BitWriter;
using test_data::FromHex;
using ::testing::ElementsAreArray;

std::vector<std::uint8_t> Rbsp(BitWriter bits) { return bits.Rbsp(); }

TEST(AacTest, ParsesTheConfigOfAnFfmpegStream) {
  const absl::StatusOr<AacConfig> config =
      ParseAudioSpecificConfig(FromHex(test_data::kAscAacMono));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->audio_object_type, 2);
  EXPECT_EQ(config->sampling_frequency, 48000);
  EXPECT_EQ(config->channel_configuration, 1);
  EXPECT_EQ(config->channel_count, 1);
  EXPECT_EQ(config->channel_mask, 0x4U);
  EXPECT_FALSE(config->sbr);
  EXPECT_FALSE(config->ps);
  EXPECT_FALSE(config->frame_length_960);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "mp4a.40.2");
  const penumbra::AudioInfo info = ToAudioInfo(*config);
  EXPECT_EQ(info.sample_rate, 48000);
  EXPECT_EQ(info.channel_count, 1);
}

TEST(AacTest, ParsesAFiveDotOneConfig) {
  const absl::StatusOr<AacConfig> config =
      ParseAudioSpecificConfig(FromHex(test_data::kAscAac51));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->channel_count, 6);
  EXPECT_EQ(config->channel_mask, 0x60fU);
  EXPECT_EQ(ToAudioInfo(*config).channel_count, 6);
}

TEST(AacTest, ExplicitHierarchicalSbrUsesObjectType5) {
  BitWriter bits;
  bits.Put(5, 5).Put(6, 4).Put(2, 4).Put(3, 4).Put(2, 5);
  bits.Flag(false).Flag(false).Flag(false);
  const absl::StatusOr<AacConfig> config = ParseAudioSpecificConfig(Rbsp(bits));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->audio_object_type, 2);
  EXPECT_TRUE(config->sbr);
  EXPECT_FALSE(config->ps);
  EXPECT_EQ(config->sampling_frequency, 24000);
  EXPECT_EQ(config->extension_sampling_frequency, 48000);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "mp4a.40.5");
  const penumbra::AudioInfo info = ToAudioInfo(*config);
  EXPECT_EQ(info.sample_rate, 48000);
  EXPECT_EQ(info.channel_count, 2);
}

TEST(AacTest, ExplicitParametricStereoUsesObjectType29) {
  BitWriter bits;
  bits.Put(29, 5).Put(6, 4).Put(1, 4).Put(3, 4).Put(2, 5);
  bits.Flag(false).Flag(false).Flag(false);
  const absl::StatusOr<AacConfig> config = ParseAudioSpecificConfig(Rbsp(bits));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_TRUE(config->sbr);
  EXPECT_TRUE(config->ps);
  EXPECT_EQ(config->channel_count, 1);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "mp4a.40.29");
  const penumbra::AudioInfo info = ToAudioInfo(*config);
  EXPECT_EQ(info.channel_count, 2);
  EXPECT_EQ(info.sample_rate, 48000);
}

TEST(AacTest, BackwardCompatibleSignallingFindsSbrAndPs) {
  BitWriter sbr;
  sbr.Put(2, 5).Put(6, 4).Put(2, 4).Flag(false).Flag(false).Flag(false);
  sbr.Put(0x2b7, 11).Put(5, 5).Flag(true).Put(3, 4);
  absl::StatusOr<AacConfig> config = ParseAudioSpecificConfig(Rbsp(sbr));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_TRUE(config->sbr);
  EXPECT_FALSE(config->ps);
  EXPECT_EQ(config->OutputSampleRate(), 48000);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "mp4a.40.5");

  BitWriter ps;
  ps.Put(2, 5).Put(6, 4).Put(1, 4).Flag(false).Flag(false).Flag(false);
  ps.Put(0x2b7, 11).Put(5, 5).Flag(true).Put(3, 4).Put(0x548, 11).Flag(true);
  config = ParseAudioSpecificConfig(Rbsp(ps));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_TRUE(config->sbr);
  EXPECT_TRUE(config->ps);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "mp4a.40.29");
}

TEST(AacTest, ReadsExplicitSamplingFrequenciesAndEscapedObjectTypes) {
  BitWriter explicit_rate;
  explicit_rate.Put(2, 5).Put(15, 4).Put(11000, 24).Put(2, 4);
  explicit_rate.Flag(false).Flag(false).Flag(false);
  const absl::StatusOr<AacConfig> config =
      ParseAudioSpecificConfig(Rbsp(explicit_rate));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->sampling_frequency, 11000);

  BitWriter usac;
  usac.Put(31, 5).Put(10, 6).Put(3, 4).Put(2, 4);
  EXPECT_EQ(ParseAudioSpecificConfig(Rbsp(usac)).status().code(),
            absl::StatusCode::kUnimplemented);
}

TEST(AacTest, ReadsTheChannelCountFromAProgramConfigElement) {
  BitWriter bits;
  bits.Put(2, 5).Put(4, 4).Put(0, 4).Flag(false).Flag(false).Flag(false);
  bits.Put(1, 4).Put(1, 2).Put(4, 4);
  bits.Put(1, 4).Put(1, 4).Put(1, 4).Put(1, 2).Put(0, 3).Put(0, 4);
  bits.Flag(false).Flag(false).Flag(false);
  bits.Flag(false).Put(0, 4);
  bits.Flag(true).Put(1, 4);
  bits.Flag(true).Put(2, 4);
  bits.Put(0, 4);
  bits.Align().Put(0, 8);
  const absl::StatusOr<AacConfig> config = ParseAudioSpecificConfig(Rbsp(bits));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->channel_configuration, 0);
  EXPECT_EQ(config->channel_count, 6);
}

TEST(AacTest, TruncationAndReservedValuesAreErrors) {
  const std::vector<std::uint8_t> asc = FromHex(test_data::kAscAacMono);
  for (std::size_t length = 0; length < 2; ++length) {
    EXPECT_EQ(
        ParseAudioSpecificConfig(Bytes(asc).first(length)).status().code(),
        absl::StatusCode::kDataLoss)
        << length;
  }
  EXPECT_EQ(ParseAudioSpecificConfig(FromHex("1788")).status().code(),
            absl::StatusCode::kDataLoss);
  BitWriter bad_channels;
  bad_channels.Put(2, 5).Put(4, 4).Put(8, 4).Flag(false).Flag(false).Flag(
      false);
  EXPECT_EQ(ParseAudioSpecificConfig(Rbsp(bad_channels)).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(OpusTest, ParsesTheDOpsBoxOfAnFfmpegStream) {
  const absl::StatusOr<OpusConfig> config =
      ParseDOps(FromHex(test_data::kDOps));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->channel_count, 1);
  EXPECT_EQ(config->pre_skip, 312);
  EXPECT_EQ(config->input_sample_rate, 48000U);
  EXPECT_EQ(config->output_gain, 0);
  EXPECT_EQ(config->mapping_family, 0);
  EXPECT_EQ(config->stream_count, 1);
  EXPECT_EQ(config->coupled_count, 0);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "opus");
  const penumbra::AudioInfo info = ToAudioInfo(*config);
  EXPECT_EQ(info.sample_rate, 48000);
  EXPECT_EQ(info.channel_mask, 0x4U);
}

TEST(OpusTest, ParsesOpusHeadAndAgreesWithDOps) {
  const std::vector<std::uint8_t> head =
      FromHex("4f707573486561640102380180bb0000f6ff00");
  const absl::StatusOr<OpusConfig> from_head = ParseOpusHead(head);
  ASSERT_TRUE(from_head.ok()) << from_head.status();
  EXPECT_EQ(from_head->channel_count, 2);
  EXPECT_EQ(from_head->pre_skip, 312);
  EXPECT_EQ(from_head->input_sample_rate, 48000U);
  EXPECT_EQ(from_head->output_gain, -10);
  EXPECT_EQ(from_head->coupled_count, 1);
  const absl::StatusOr<std::vector<std::uint8_t>> box =
      SerializeDOps(*from_head);
  ASSERT_TRUE(box.ok());
  const absl::StatusOr<OpusConfig> from_box = ParseDOps(*box);
  ASSERT_TRUE(from_box.ok());
  EXPECT_EQ(*from_box, *from_head);
}

TEST(OpusTest, ParsesSurroundMappingTables) {
  const std::vector<std::uint8_t> box =
      FromHex("000601380000bb8000000104020004010203ff");
  const absl::StatusOr<OpusConfig> config = ParseDOps(box);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->channel_count, 6);
  EXPECT_EQ(config->mapping_family, 1);
  EXPECT_EQ(config->stream_count, 4);
  EXPECT_EQ(config->coupled_count, 2);
  EXPECT_THAT(config->channel_mapping,
              ElementsAreArray(FromHex("0004010203ff")));
  EXPECT_EQ(ToAudioInfo(*config).channel_mask, 0x3fU);
  EXPECT_THAT(*SerializeDOps(*config), ElementsAreArray(box));
}

TEST(OpusTest, RejectsInvalidConfigurations) {
  EXPECT_EQ(ParseOpusHead(FromHex("4f7075734865616e0102380180bb00000000"))
                .status()
                .code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseOpusHead(FromHex("4f707573486561641102380180bb0000000000"))
                .status()
                .code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(ParseDOps(FromHex("01010138")).status().code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(ParseDOps(FromHex("00000138000bb80000000000")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseDOps(FromHex("00030138000bb80000000000")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseDOps(FromHex("00020138000bb800000001010100")).status().code(),
            absl::StatusCode::kDataLoss);
  const std::vector<std::uint8_t> box = FromHex(test_data::kDOps);
  for (std::size_t length = 0; length < box.size(); ++length) {
    EXPECT_EQ(ParseDOps(Bytes(box).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
}

TEST(Ac3Test, ParsesTheDac3BoxOfAnFfmpegStream) {
  const absl::StatusOr<Ac3Config> config = ParseDac3(FromHex(test_data::kDac3));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->fscod, 0);
  EXPECT_EQ(config->bsid, 8);
  EXPECT_EQ(config->acmod, 1);
  EXPECT_FALSE(config->lfeon);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "ac-3");
  const penumbra::AudioInfo info = ToAudioInfo(*config);
  EXPECT_EQ(info.sample_rate, 48000);
  EXPECT_EQ(info.channel_count, 1);
  const absl::StatusOr<std::vector<std::uint8_t>> again =
      SerializeDac3(*config);
  ASSERT_TRUE(again.ok());
  EXPECT_THAT(*again, ElementsAreArray(FromHex(test_data::kDac3)));
}

TEST(Ac3Test, CountsLfeAndRejectsReservedRates) {
  Ac3Config config;
  config.fscod = 1;
  config.acmod = 7;
  config.lfeon = true;
  const absl::StatusOr<std::vector<std::uint8_t>> box = SerializeDac3(config);
  ASSERT_TRUE(box.ok());
  const absl::StatusOr<Ac3Config> parsed = ParseDac3(*box);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(*parsed, config);
  const penumbra::AudioInfo info = ToAudioInfo(*parsed);
  EXPECT_EQ(info.sample_rate, 44100);
  EXPECT_EQ(info.channel_count, 6);
  EXPECT_EQ(info.channel_mask, 0x60fU);
  EXPECT_EQ(ParseDac3(FromHex("c008c0")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseDac3(FromHex("1008")).status().code(),
            absl::StatusCode::kDataLoss);
}

TEST(Eac3Test, ParsesTheDec3BoxOfAnFfmpegStream) {
  const absl::StatusOr<Eac3Config> config =
      ParseDec3(FromHex(test_data::kDec3));
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->data_rate, 96);
  ASSERT_EQ(config->substreams.size(), 1U);
  EXPECT_EQ(config->substreams[0].fscod, 0);
  EXPECT_EQ(config->substreams[0].bsid, 16);
  EXPECT_EQ(config->substreams[0].acmod, 1);
  EXPECT_FALSE(config->joc);
  EXPECT_EQ(FormatCodecString(ToCodec(*config)), "ec-3");
  EXPECT_EQ(ToAudioInfo(*config).channel_count, 1);
  EXPECT_THAT(*SerializeDec3(*config),
              ElementsAreArray(FromHex(test_data::kDec3)));
}

TEST(Eac3Test, AddsDependentSubstreamChannelsAndJocSignalling) {
  Eac3Config config;
  config.data_rate = 768;
  Eac3Substream main;
  main.fscod = 0;
  main.bsid = 16;
  main.acmod = 7;
  main.lfeon = true;
  main.num_dependent_substreams = 1;
  main.chan_loc = 0x2;
  config.substreams.push_back(main);
  config.joc = true;
  config.joc_complexity_index = 16;
  const absl::StatusOr<std::vector<std::uint8_t>> box = SerializeDec3(config);
  ASSERT_TRUE(box.ok()) << box.status();
  const absl::StatusOr<Eac3Config> parsed = ParseDec3(*box);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, config);
  const penumbra::AudioInfo info = ToAudioInfo(*parsed);
  EXPECT_EQ(info.channel_count, 8);
  EXPECT_EQ(info.channel_mask, 0x63fU);
}

TEST(Eac3Test, RejectsTruncatedAndReservedConfigurations) {
  const std::vector<std::uint8_t> box = FromHex(test_data::kDec3);
  for (std::size_t length = 0; length < box.size(); ++length) {
    EXPECT_EQ(ParseDec3(Bytes(box).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
  EXPECT_EQ(ParseDec3(FromHex("0300200208")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(ParseDec3(FromHex("0300e00200")).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(SerializeDec3({}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(FlacTest, ParsesTheDfLaBoxOfAnFfmpegStream) {
  const absl::StatusOr<FlacStreamInfo> info =
      ParseDfLa(FromHex(test_data::kDfLa));
  ASSERT_TRUE(info.ok()) << info.status();
  EXPECT_EQ(info->min_block_size, 4608);
  EXPECT_EQ(info->max_block_size, 4608);
  EXPECT_EQ(info->min_frame_size, 200U);
  EXPECT_EQ(info->max_frame_size, 1265U);
  EXPECT_EQ(info->sample_rate, 48000U);
  EXPECT_EQ(info->channel_count, 1);
  EXPECT_EQ(info->bits_per_sample, 16);
  EXPECT_EQ(info->total_samples, 9600U);
  EXPECT_EQ(FormatCodecString(ToCodec(*info)), "fLaC");
  const penumbra::AudioInfo audio = ToAudioInfo(*info);
  EXPECT_EQ(audio.sample_rate, 48000);
  EXPECT_EQ(audio.channel_count, 1);
  EXPECT_EQ(audio.sample_format, penumbra::SampleFormat::kS16);
}

TEST(FlacTest, StreamInfoAloneParsesTheSameValues) {
  const std::vector<std::uint8_t> dfla = FromHex(test_data::kDfLa);
  const absl::StatusOr<FlacStreamInfo> direct =
      ParseFlacStreamInfo(Bytes(dfla).subspan(8));
  const absl::StatusOr<FlacStreamInfo> boxed = ParseDfLa(dfla);
  ASSERT_TRUE(direct.ok());
  ASSERT_TRUE(boxed.ok());
  EXPECT_EQ(*direct, *boxed);
}

TEST(FlacTest, RejectsInvalidStreamInfo) {
  std::vector<std::uint8_t> dfla = FromHex(test_data::kDfLa);
  for (std::size_t length = 0; length < dfla.size(); ++length) {
    EXPECT_EQ(ParseDfLa(Bytes(dfla).first(length)).status().code(),
              absl::StatusCode::kDataLoss)
        << length;
  }
  std::vector<std::uint8_t> small_block = dfla;
  small_block[8] = 0;
  small_block[9] = 8;
  EXPECT_EQ(ParseDfLa(small_block).status().code(),
            absl::StatusCode::kDataLoss);
  std::vector<std::uint8_t> wrong_type = dfla;
  wrong_type[4] = 0x84;
  EXPECT_EQ(ParseDfLa(wrong_type).status().code(), absl::StatusCode::kDataLoss);
  std::vector<std::uint8_t> versioned = dfla;
  versioned[0] = 1;
  EXPECT_EQ(ParseDfLa(versioned).status().code(),
            absl::StatusCode::kUnimplemented);
}

}
}
