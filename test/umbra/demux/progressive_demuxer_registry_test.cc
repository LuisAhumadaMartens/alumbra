#include "umbra/demux/progressive_demuxer_registry.h"

#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "test_support.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/demux/packed_audio_demuxer.h"
#include "umbra/source/source_type.h"

namespace umbra::demux::test {
namespace {

TEST(ProgressiveDemuxerRegistryTest, CreatesRegisteredDemuxers) {
  RecordingOutput output;
  const auto mp4 = CreateProgressiveDemuxer(source::SourceType::kMp4, output);
  ASSERT_TRUE(mp4.ok());
  EXPECT_NE(dynamic_cast<Mp4Demuxer*>(mp4->get()), nullptr);

  const auto adts = CreateProgressiveDemuxer(source::SourceType::kAdts, output);
  ASSERT_TRUE(adts.ok());
  EXPECT_NE(dynamic_cast<AdtsDemuxer*>(adts->get()), nullptr);

  const auto mp3 = CreateProgressiveDemuxer(source::SourceType::kMp3, output);
  ASSERT_TRUE(mp3.ok());
  EXPECT_NE(dynamic_cast<Mp3Demuxer*>(mp3->get()), nullptr);
}

TEST(ProgressiveDemuxerRegistryTest, MissingDemuxersReturnUnsupportedStatus) {
  RecordingOutput output;
  for (const source::SourceType type :
       {source::SourceType::kMpegTs, source::SourceType::kMatroska}) {
    const auto result = CreateProgressiveDemuxer(type, output);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_NE(result.status().message().find("source not supported"),
              std::string_view::npos);
  }
}

}
}
