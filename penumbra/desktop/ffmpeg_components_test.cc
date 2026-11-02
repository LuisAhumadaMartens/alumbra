#include <cstddef>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
}

namespace penumbra::desktop {
namespace {

using ::testing::ContainsRegex;
using ::testing::Not;
using ::testing::UnorderedElementsAreArray;

struct Cursor {
  void* state = nullptr;
};

std::map<std::string, std::set<std::string>> ReadComponents() {
  std::ifstream file(ALUMBRA_FFMPEG_COMPONENTS_FILE);
  std::map<std::string, std::set<std::string>> components;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = absl::StrSplit(line, ' ');
    EXPECT_TRUE(fields.size() == 2 ||
                (fields.size() == 3 && fields[2] == "patent"))
        << line;
    if (fields.size() >= 2) {
      components[fields[0]].insert(fields[1]);
    }
  }
  return components;
}

std::set<std::string> Split(const char* names) {
  const std::vector<std::string> parts = absl::StrSplit(names, ',');
  return {parts.begin(), parts.end()};
}

TEST(FfmpegComponentsTest, TheListFileIsNotEmpty) {
  const auto components = ReadComponents();
  EXPECT_EQ(components.at("decoder").size(), 9U);
  EXPECT_FALSE(components.at("parser").empty());
  EXPECT_FALSE(components.at("demuxer").empty());
}

TEST(FfmpegComponentsTest, ConfigureLineCarriesTheReviewedBaseline) {
  const std::string configuration = avcodec_configuration();
  for (const char* flag :
       {"--disable-gpl", "--disable-version3", "--disable-nonfree",
        "--enable-shared", "--disable-static", "--disable-autodetect",
        "--disable-programs", "--disable-doc", "--disable-network",
        "--disable-protocols", "--disable-everything", "--disable-avdevice",
        "--disable-avfilter", "--disable-swscale"}) {
    EXPECT_THAT(configuration, ::testing::HasSubstr(flag)) << flag;
  }
  EXPECT_THAT(configuration,
              Not(ContainsRegex("--enable-(gpl|version3|nonfree)")));
  EXPECT_THAT(
      configuration,
      Not(ContainsRegex(
          "--enable-(encoder|muxer|filter|protocol|hwaccel|indev|outdev)")));
  EXPECT_THAT(configuration, Not(ContainsRegex("--enable-lib")));
  EXPECT_THAT(
      configuration,
      Not(ContainsRegex(
          "--enable-(videotoolbox|audiotoolbox|vaapi|vdpau|nvdec|cuda)")));
}

TEST(FfmpegComponentsTest, DecodersAreExactlyTheListedNativeOnes) {
  std::vector<std::string> decoders;
  Cursor cursor;
  while (const AVCodec* codec = av_codec_iterate(&cursor.state)) {
    EXPECT_NE(av_codec_is_decoder(codec), 0) << "encoder " << codec->name;
    decoders.emplace_back(codec->name);
  }
  const auto components = ReadComponents();
  const auto& expected = components.at("decoder");
  EXPECT_THAT(decoders, UnorderedElementsAreArray(expected));
}

TEST(FfmpegComponentsTest, DemuxersAreExactlyTheListedOnes) {
  std::set<std::string> found;
  Cursor cursor;
  while (const AVInputFormat* format = av_demuxer_iterate(&cursor.state)) {
    const auto names = Split(format->name);
    found.insert(names.begin(), names.end());
  }
  const auto components = ReadComponents();
  const auto& expected = components.at("demuxer");
  for (const std::string& name : expected) {
    EXPECT_TRUE(found.contains(name)) << name;
  }
  std::size_t formats = 0;
  cursor = Cursor();
  while (av_demuxer_iterate(&cursor.state) != nullptr) {
    ++formats;
  }
  EXPECT_EQ(formats, expected.size());
}

TEST(FfmpegComponentsTest, ParsersAreExactlyTheListedOnes) {
  const std::map<std::string, AVCodecID> ids = {
      {"h264", AV_CODEC_ID_H264},     {"hevc", AV_CODEC_ID_HEVC},
      {"vp9", AV_CODEC_ID_VP9},       {"av1", AV_CODEC_ID_AV1},
      {"aac", AV_CODEC_ID_AAC},       {"opus", AV_CODEC_ID_OPUS},
      {"mpegaudio", AV_CODEC_ID_MP3}, {"ac3", AV_CODEC_ID_AC3},
      {"flac", AV_CODEC_ID_FLAC}};
  const auto components = ReadComponents();
  const auto& expected = components.at("parser");
  EXPECT_EQ(expected.size(), ids.size());
  std::size_t count = 0;
  Cursor cursor;
  while (av_parser_iterate(&cursor.state) != nullptr) {
    ++count;
  }
  EXPECT_EQ(count, expected.size());
  for (const std::string& name : expected) {
    const auto id = ids.find(name);
    ASSERT_NE(id, ids.end()) << name;
    AVCodecParserContext* parser = av_parser_init(id->second);
    EXPECT_NE(parser, nullptr) << name;
    av_parser_close(parser);
  }
}

TEST(FfmpegComponentsTest, NoEncodersMuxersProtocolsOrFiltersAreBuilt) {
  Cursor cursor;
  EXPECT_EQ(av_muxer_iterate(&cursor.state), nullptr);
  cursor = Cursor();
  EXPECT_EQ(avio_enum_protocols(&cursor.state, 0), nullptr);
  cursor = Cursor();
  EXPECT_EQ(avio_enum_protocols(&cursor.state, 1), nullptr);
}

TEST(FfmpegComponentsTest, OnlyTheDecoderDependenciesAmongBitstreamFilters) {
  std::vector<std::string> filters;
  Cursor cursor;
  while (const AVBitStreamFilter* filter = av_bsf_iterate(&cursor.state)) {
    filters.emplace_back(filter->name);
  }
  EXPECT_THAT(filters, ::testing::ElementsAre("vp9_superframe_split"));
}

TEST(FfmpegComponentsTest, NativeAv1DecodingIsAbsent) {
  EXPECT_EQ(avcodec_find_decoder(AV_CODEC_ID_AV1), nullptr);
  EXPECT_EQ(avcodec_find_decoder_by_name("libdav1d"), nullptr);
}

}
}
