#include "umbra/demux/mkv_tracks.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/mkv_elements.h"

namespace umbra::demux {
namespace {

TEST(MkvTrackConfigTest, ExposesAssCodecPrivateAsTextTrackConfiguration) {
  MkvTrack track;
  track.number = 4;
  track.type = kTrackTypeSubtitle;
  track.codec_id = "S_TEXT/ASS";
  track.codec_private = {'[', 'E', 'v', 'e', 'n', 't', 's', ']'};
  track.language = "jpn";

  const auto config = BuildTrackConfig(track, penumbra::Rational::Of(1, 1000));

  ASSERT_TRUE(config.ok());
  ASSERT_TRUE(config->has_value());
  EXPECT_EQ((*config)->stream.track_id, 4U);
  EXPECT_EQ((*config)->stream.codec_string, "ass");
  EXPECT_EQ((*config)->stream.language, "jpn");
  EXPECT_EQ((*config)->stream.type(), penumbra::StreamType::kText);
  EXPECT_EQ((*config)->stream.codec_config.span().size(),
            track.codec_private.size());
}

TEST(MkvTrackConfigTest, ExposesSsaSubtitleTrack) {
  MkvTrack track;
  track.number = 2;
  track.type = kTrackTypeSubtitle;
  track.codec_id = "S_TEXT/SSA";

  const auto config = BuildTrackConfig(track, penumbra::Rational::Of(1, 1000));

  ASSERT_TRUE(config.ok());
  ASSERT_TRUE(config->has_value());
  EXPECT_EQ((*config)->stream.codec_string, "ssa");
  EXPECT_TRUE(
      std::holds_alternative<penumbra::TextInfo>((*config)->stream.format));
}

}
}
