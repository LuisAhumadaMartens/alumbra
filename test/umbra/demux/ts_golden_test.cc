#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"
#include "mp4_builder.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/stream_info.h"
#include "test_support.h"
#include "umbra/demux/ts_demuxer.h"

namespace umbra::demux::test {
namespace {

Bytes ReadFile(const std::string& name) {
  std::ifstream stream(std::string(ALUMBRA_DEMUX_FIXTURE_DIR) + "/" + name,
                       std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string Render(const RecordingOutput& output) {
  std::string text;
  for (size_t index = 0; index < output.streams.size(); ++index) {
    const penumbra::StreamInfo& stream = output.streams[index];
    absl::StrAppend(&text, "stream ", index, " ",
                    penumbra::CodecName(stream.codec), " ",
                    stream.time_base.ToString(), "\n");
    for (const penumbra::EncodedSample& sample :
         output.SamplesOf(stream.track_id)) {
      absl::StrAppend(&text, sample.pts.value, " ", sample.dts.value, " ",
                      sample.duration.value, " ", sample.data.size(),
                      sample.key_frame ? " K" : " _", "\n");
    }
  }
  return text;
}

std::string MaskLastDurations(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream input(text);
  for (std::string line; std::getline(input, line);) {
    lines.push_back(line);
  }
  std::string result;
  for (size_t index = 0; index < lines.size(); ++index) {
    const bool last =
        index + 1 == lines.size() || lines[index + 1].starts_with("stream ");
    std::string line = lines[index];
    if (last && !line.starts_with("stream ")) {
      std::istringstream fields(line);
      std::string pts;
      std::string dts;
      std::string duration;
      std::string rest;
      fields >> pts >> dts >> duration;
      std::getline(fields, rest);
      line = absl::StrCat(pts, " ", dts, " -", rest);
    }
    absl::StrAppend(&result, line, "\n");
  }
  return result;
}

class TsGoldenTest : public ::testing::TestWithParam<const char*> {};

TEST_P(TsGoldenTest, PacketsMatchFfprobe) {
  const std::string name = GetParam();
  const Bytes file = ReadFile(name);
  ASSERT_FALSE(file.empty());
  const Bytes expected_bytes = ReadFile(name + ".packets");
  ASSERT_FALSE(expected_bytes.empty());
  const std::string expected = MaskLastDurations(absl::StrReplaceAll(
      std::string(expected_bytes.begin(), expected_bytes.end()),
      {{"mp2", "mp3"}}));
  for (const size_t chunk : {size_t{1} << 20, size_t{1500}, size_t{97}}) {
    RecordingOutput output;
    TsDemuxer demuxer(output);
    const DriveResult result = DriveBytes(demuxer, file, chunk);
    ASSERT_TRUE(result.status.ok()) << result.status;
    EXPECT_EQ(MaskLastDurations(Render(output)), expected) << "chunk " << chunk;
  }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, TsGoldenTest,
                         ::testing::Values("h264_aac.ts", "hevc_ac3.ts",
                                           "mp2_eac3.ts"));

}
}
