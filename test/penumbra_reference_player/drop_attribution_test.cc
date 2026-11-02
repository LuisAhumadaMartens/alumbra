#include "penumbra/reference_player/drop_attribution.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"

namespace penumbra {
namespace {

TimeDelta Ms(int64_t value) { return TimeDelta::FromMilliseconds(value); }

void Due(DropAttribution& drops, int count, int64_t start_ms, int64_t step_ms) {
  for (int index = 0; index < count; ++index) {
    EXPECT_FALSE(drops.RecordFrameDue(Ms(start_ms + (int64_t{index} * step_ms)))
                     .has_value());
  }
}

TEST(DropAttributionTest, FiresWhenDecodeLateDropsExceedThePercentage) {
  DropAttribution drops(10, Ms(5000));
  Due(drops, 100, 0, 40);
  for (int index = 0; index < 11; ++index) {
    EXPECT_FALSE(
        drops.RecordDrop(DropCause::kDecodeLate, Ms(int64_t{index} * 300))
            .has_value());
  }
  const auto event = drops.RecordFrameDue(Ms(5000));
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->decode_late, 11U);
  EXPECT_EQ(event->frames_due, 100U);
  EXPECT_EQ(event->max_percent, 10);
  EXPECT_EQ(event->window, Ms(5000));
  EXPECT_EQ(drops.Triggers(), 1U);
}

TEST(DropAttributionTest, ExactlyTheThresholdDoesNotFire) {
  DropAttribution drops(10, Ms(5000));
  Due(drops, 100, 0, 40);
  for (int index = 0; index < 10; ++index) {
    EXPECT_FALSE(
        drops.RecordDrop(DropCause::kDecodeLate, Ms(int64_t{index} * 300))
            .has_value());
  }
  EXPECT_FALSE(drops.RecordFrameDue(Ms(5000)).has_value());
  EXPECT_EQ(drops.Triggers(), 0U);
}

TEST(DropAttributionTest, PresentLateDropsNeverTrigger) {
  DropAttribution drops(10, Ms(5000));
  Due(drops, 100, 0, 40);
  for (int index = 0; index < 60; ++index) {
    EXPECT_FALSE(
        drops.RecordDrop(DropCause::kPresentLate, Ms(int64_t{index} * 50))
            .has_value());
  }
  EXPECT_FALSE(drops.RecordFrameDue(Ms(5000)).has_value());
  EXPECT_EQ(drops.PresentLate(), 60U);
  EXPECT_EQ(drops.DecodeLate(), 0U);
  EXPECT_EQ(drops.Triggers(), 0U);
}

TEST(DropAttributionTest, EachWindowIsEvaluatedOnItsOwn) {
  DropAttribution drops(10, Ms(1000));
  Due(drops, 10, 0, 100);
  drops.RecordDrop(DropCause::kDecodeLate, Ms(500));
  drops.RecordDrop(DropCause::kDecodeLate, Ms(600));
  EXPECT_TRUE(drops.Poll(Ms(1000)).has_value());
  Due(drops, 10, 1000, 100);
  EXPECT_FALSE(drops.Poll(Ms(2000)).has_value());
  EXPECT_EQ(drops.Triggers(), 1U);
  EXPECT_EQ(drops.DecodeLate(), 2U);
}

TEST(DropAttributionTest, RestartingTheWindowDiscardsStartUpWork) {
  DropAttribution drops(10, Ms(1000));
  Due(drops, 5, 0, 100);
  drops.RecordDrop(DropCause::kDecodeLate, Ms(400));
  drops.RestartWindow();
  EXPECT_FALSE(drops.Poll(Ms(1000)).has_value());
  EXPECT_FALSE(drops.Poll(Ms(2500)).has_value());
  EXPECT_EQ(drops.Triggers(), 0U);
  EXPECT_EQ(drops.DecodeLate(), 1U);
}

TEST(DropAttributionTest, ThresholdsCanChangeLive) {
  DropAttribution drops(50, Ms(1000));
  Due(drops, 10, 0, 100);
  for (int index = 0; index < 3; ++index) {
    drops.RecordDrop(DropCause::kDecodeLate, Ms(int64_t{index} * 100));
  }
  drops.Reconfigure(10, Ms(1000));
  EXPECT_TRUE(drops.Poll(Ms(1000)).has_value());
}

TEST(DropAttributionTest, NeverFiresWithoutFramesDue) {
  DropAttribution drops(0, Ms(1000));
  drops.RecordDrop(DropCause::kDecodeLate, Ms(0));
  EXPECT_FALSE(drops.Poll(Ms(5000)).has_value());
}

}
}
