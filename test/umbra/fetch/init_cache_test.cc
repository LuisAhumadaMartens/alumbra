#include "umbra/fetch/init_cache.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/manifest/segment.h"

namespace umbra::fetch {
namespace {

penumbra::SharedBytes Bytes(uint8_t value) {
  return penumbra::SharedBytes::Adopt(std::vector<uint8_t>(4, value));
}

TEST(InitCacheTest, KeysSeparateUrlAndRange) {
  const manifest::ByteRange first{.offset = 0, .length = 10};
  const manifest::ByteRange second{.offset = 10, .length = 10};
  EXPECT_NE(InitCacheKey("https://a.test/i", first),
            InitCacheKey("https://a.test/i", second));
  EXPECT_NE(InitCacheKey("https://a.test/i", first),
            InitCacheKey("https://a.test/i", std::nullopt));
  EXPECT_EQ(InitCacheKey("https://a.test/i", first),
            InitCacheKey("https://a.test/i", first));
}

TEST(InitCacheTest, FindsInsertedBytes) {
  InitCache cache(2);
  EXPECT_FALSE(cache.Find("a").has_value());
  cache.Insert("a", Bytes(1));
  const auto found = cache.Find("a");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->span().front(), 1);
}

TEST(InitCacheTest, EvictsLeastRecentlyUsed) {
  InitCache cache(2);
  cache.Insert("a", Bytes(1));
  cache.Insert("b", Bytes(2));
  EXPECT_TRUE(cache.Find("a").has_value());
  cache.Insert("c", Bytes(3));
  EXPECT_EQ(cache.size(), 2U);
  EXPECT_TRUE(cache.Find("a").has_value());
  EXPECT_FALSE(cache.Find("b").has_value());
  EXPECT_TRUE(cache.Find("c").has_value());
}

TEST(InitCacheTest, ReplacesExistingEntryWithoutGrowing) {
  InitCache cache(2);
  cache.Insert("a", Bytes(1));
  cache.Insert("a", Bytes(9));
  EXPECT_EQ(cache.size(), 1U);
  EXPECT_EQ(cache.Find("a")->span().front(), 9);
  cache.Clear();
  EXPECT_EQ(cache.size(), 0U);
}

}
}
