#include <cmath>
#include <map>
#include <set>
#include <string>
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

TEST(MediaErrorMappingTest, EveryCatalogueEntryMapsToItsDeclaredCode) {
  for (const auto& entry : base::kErrorCatalogue) {
    const base::ErrorDetail detail(entry.code, entry.category, entry.severity,
                                   entry.retryable, {}, nullptr);
    const int code = static_cast<int>(MediaErrorCodeFor(detail));
    EXPECT_EQ(code, entry.media_error) << entry.name;
    EXPECT_GE(code, 1);
    EXPECT_LE(code, 4);
  }
}

TEST(MediaErrorMappingTest, EveryCategoryHasExactlyOneFallbackCode) {
  const std::map<base::ErrorCategory, MediaErrorCode> expected{
      {base::ErrorCategory::kNetwork, MediaErrorCode::kNetwork},
      {base::ErrorCategory::kManifest, MediaErrorCode::kSrcNotSupported},
      {base::ErrorCategory::kDemux, MediaErrorCode::kDecode},
      {base::ErrorCategory::kDecode, MediaErrorCode::kDecode},
      {base::ErrorCategory::kDrm, MediaErrorCode::kDecode},
      {base::ErrorCategory::kText, MediaErrorCode::kDecode},
      {base::ErrorCategory::kAds, MediaErrorCode::kNetwork},
      {base::ErrorCategory::kPlayer, MediaErrorCode::kDecode},
  };
  EXPECT_EQ(expected.size(), 8U);
  for (const auto& [category, code] : expected) {
    EXPECT_EQ(MediaErrorCodeFor(category), code);
    base::ErrorDetail detail;
    detail.code = static_cast<base::ErrorCode>(9999);
    detail.category = category;
    EXPECT_EQ(MediaErrorCodeFor(detail), code);
  }
}

TEST(MediaErrorMappingTest, EveryCatalogueCategoryIsCovered) {
  std::set<base::ErrorCategory> seen;
  for (const auto& entry : base::kErrorCatalogue) {
    seen.insert(entry.category);
  }
  EXPECT_EQ(seen.size(), 8U);
}

TEST(MediaErrorTest, ExtendedDetailComesFromTheContext) {
  base::ErrorDetail detail;
  detail.context = {{"platform_error_code", "-1004"}, {"http_status", "503"}};
  const MediaError error(MediaErrorCode::kNetwork, detail);
  EXPECT_EQ(error.category(), base::ErrorCategory::kPlayer);
  EXPECT_EQ(error.platform_code(), -1004);
  EXPECT_EQ(error.http_status(), 503);
}

TEST(MediaErrorTest, MissingOrMalformedExtendedDetailIsEmpty) {
  const MediaError none(MediaErrorCode::kNetwork, base::ErrorDetail());
  EXPECT_FALSE(none.platform_code().has_value());
  EXPECT_FALSE(none.http_status().has_value());
  base::ErrorDetail detail;
  detail.context = {{"platform_error_code", "12x"}, {"http_status", ""}};
  const MediaError bad(MediaErrorCode::kNetwork, detail);
  EXPECT_FALSE(bad.platform_code().has_value());
  EXPECT_FALSE(bad.http_status().has_value());
}

TEST(MediaErrorTest, MessageNeverContainsContextValues) {
  base::ErrorDetail detail;
  detail.code = base::ErrorCode::kNetworkRequestFailed;
  detail.context = {{"request_url", "https://host.test/path?token=secret"}};
  const MediaError error(MediaErrorCode::kNetwork, detail);
  EXPECT_EQ(error.message(), "ALUMBRA-1003: Network request failed");
  EXPECT_EQ(error.message().find("secret"), std::string::npos);
  EXPECT_EQ(error.message().find("host.test"), std::string::npos);
}
}
}
