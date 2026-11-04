#include "umbra/text/caption_layout.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace umbra::text {
namespace {

TEST(CaptionLayoutTest, SnapsBottomAnchoredCueToSafeAreaLine) {
  CaptionCueLayout cue;
  cue.width = 120;
  cue.height = 20;
  cue.line_height = 20;
  cue.snap_line = -1;
  const std::array cues = {cue};
  CaptionLayoutInput input;
  input.viewport_width = 640;
  input.viewport_height = 360;
  input.safe_area = {.top = 18, .right = 24, .bottom = 30, .left = 24};
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 1U);
  EXPECT_FLOAT_EQ(result[0].rect.x, 260.0F);
  EXPECT_FLOAT_EQ(result[0].rect.y, 310.0F);
}

TEST(CaptionLayoutTest, MovesOverlappingBottomCuesTowardTheTop) {
  CaptionCueLayout first;
  first.width = 200;
  first.height = 24;
  first.line_height = 24;
  const CaptionCueLayout second = first;
  const std::array cues = {first, second};
  CaptionLayoutInput input;
  input.viewport_width = 640;
  input.viewport_height = 360;
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 2U);
  EXPECT_FLOAT_EQ(result[0].rect.y, 336.0F);
  EXPECT_FLOAT_EQ(result[1].rect.y, 312.0F);
}

TEST(CaptionLayoutTest, ScrollsRegionCuesUpAndKeepsSeparateRegionsIndependent) {
  CaptionCueLayout first;
  first.width = 200;
  first.height = 24;
  first.line_height = 24;
  first.region_id = 1;
  first.region_scroll_up = true;
  const CaptionCueLayout second = first;
  CaptionCueLayout separate = first;
  separate.region_id = 2;
  const std::array cues = {first, second, separate};
  CaptionLayoutInput input;
  input.viewport_width = 640;
  input.viewport_height = 360;
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 3U);
  EXPECT_FLOAT_EQ(result[0].rect.y, 336.0F);
  EXPECT_FLOAT_EQ(result[1].rect.y, 312.0F);
  EXPECT_FLOAT_EQ(result[2].rect.y, 336.0F);
}

TEST(CaptionLayoutTest, AppliesPercentagePositionAndSize) {
  CaptionCueLayout cue;
  cue.width = 400;
  cue.height = 30;
  cue.position_percent = 75;
  cue.size_percent = 50;
  cue.line_percent = 60;
  cue.alignment = CaptionAlignment::kCentre;
  const std::array cues = {cue};
  CaptionLayoutInput input;
  input.viewport_width = 800;
  input.viewport_height = 500;
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 1U);
  EXPECT_FLOAT_EQ(result[0].rect.width, 400.0F);
  EXPECT_FLOAT_EQ(result[0].rect.x, 400.0F);
  EXPECT_FLOAT_EQ(result[0].rect.y, 300.0F);
}

TEST(CaptionLayoutTest, PlacesVerticalCuesAgainstTheSelectedBlockEdge) {
  CaptionCueLayout right_to_left;
  right_to_left.width = 40;
  right_to_left.height = 120;
  right_to_left.line_height = 40;
  right_to_left.snap_line = -1;
  right_to_left.writing_mode = CaptionWritingMode::kVerticalRl;
  CaptionCueLayout left_to_right;
  left_to_right.width = 40;
  left_to_right.height = 120;
  left_to_right.line_height = 40;
  left_to_right.writing_mode = CaptionWritingMode::kVerticalLr;
  const std::array cues = {right_to_left, left_to_right};
  CaptionLayoutInput input;
  input.viewport_width = 400;
  input.viewport_height = 300;
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 2U);
  EXPECT_FLOAT_EQ(result[0].rect.x, 360.0F);
  EXPECT_FLOAT_EQ(result[1].rect.x, 0.0F);
}

TEST(CaptionLayoutTest, MapsGridCueIntoSafeAreaCells) {
  CaptionCueLayout cue;
  cue.width = 30;
  cue.height = 20;
  cue.grid_positioned = true;
  cue.grid_column = 8;
  cue.grid_row = 10;
  const std::array cues = {cue};
  CaptionLayoutInput input;
  input.viewport_width = 640;
  input.viewport_height = 360;
  input.safe_area = {.top = 18, .right = 32, .bottom = 18, .left = 32};
  input.cues = cues;
  const std::vector<PositionedCaption> result = LayoutCaptions(input);

  ASSERT_EQ(result.size(), 1U);
  EXPECT_FLOAT_EQ(result[0].rect.x, 176.0F);
  EXPECT_FLOAT_EQ(result[0].rect.y, 234.0F);
}

}
}
