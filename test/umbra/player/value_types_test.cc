#include <cmath>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/player/media_error.h"
#include "umbra/player/time_ranges.h"

namespace umbra::player {
namespace {

TEST(TimeRangesTest, AnEmptyObjectHasNoRanges) {
  const TimeRanges ranges;
  EXPECT_EQ(ranges.length(), 0U);
  EXPECT_TRUE(std::isnan(ranges.start(0)));
  EXPECT_TRUE(std::isnan(ranges.end(0)));
}

TEST(TimeRangesTest, RangesAreReadByIndex) {
  const TimeRanges ranges(std::vector<TimeRange>{{.start = 0.0, .end = 1.5},
                                                 {.start = 3.0, .end = 4.0}});
  ASSERT_EQ(ranges.length(), 2U);
  EXPECT_EQ(ranges.start(0), 0.0);
  EXPECT_EQ(ranges.end(0), 1.5);
  EXPECT_EQ(ranges.start(1), 3.0);
  EXPECT_EQ(ranges.end(1), 4.0);
  EXPECT_TRUE(std::isnan(ranges.end(2)));
}

TEST(MediaErrorTest, MessageStartsWithTheCatalogueCode) {
  const base::ErrorCatalogueEntry* entry =
      base::FindError(base::ErrorCode::kMediaCorrupt);
  const MediaError error(MediaErrorCode::kDecode, {entry->code,
                                                   entry->category,
                                                   entry->severity,
                                                   entry->retryable,
                                                   {},
                                                   nullptr});
  EXPECT_EQ(error.code(), MediaErrorCode::kDecode);
  EXPECT_EQ(error.message(), "ALUMBRA-3001: Media data is corrupt");
  EXPECT_EQ(error.detail().code, base::ErrorCode::kMediaCorrupt);
}

TEST(MediaErrorTest, CodesMatchTheHtmlNumbers) {
  EXPECT_EQ(static_cast<int>(MediaErrorCode::kAborted), 1);
  EXPECT_EQ(static_cast<int>(MediaErrorCode::kNetwork), 2);
  EXPECT_EQ(static_cast<int>(MediaErrorCode::kDecode), 3);
  EXPECT_EQ(static_cast<int>(MediaErrorCode::kSrcNotSupported), 4);
}

TEST(MediaErrorTest, ACodeOutsideTheCatalogueStillGetsAMessage) {
  base::ErrorDetail detail;
  detail.code = static_cast<base::ErrorCode>(9999);
  const MediaError error(MediaErrorCode::kDecode, std::move(detail));
  EXPECT_EQ(error.message(), "ALUMBRA-9999: Unknown error");
}

}
}
