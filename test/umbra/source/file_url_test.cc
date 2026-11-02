#include "umbra/source/file_url.h"

#include <string>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "umbra/source/url.h"

namespace umbra::source {
namespace {

TEST(FileUrlTest, PlainAsciiPathsAreKept) {
  const auto url = FileUrlFromPath("/home/user/movie-1.mkv");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(*url, "file:///home/user/movie-1.mkv");
}

TEST(FileUrlTest, SpacesAndReservedCharactersArePercentEncoded) {
  const auto url = FileUrlFromPath("/media/My Movies/a#b?c%d.mp4");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(*url, "file:///media/My%20Movies/a%23b%3Fc%25d.mp4");
}

TEST(FileUrlTest, NonAsciiBytesAreEncodedAsUtf8) {
  const auto url = FileUrlFromPath(
      "/media/pel\xC3\xAD"
      "cula \xE6\x98\xA0\xE7\x94\xBB.mp4");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(*url, "file:///media/pel%C3%ADcula%20%E6%98%A0%E7%94%BB.mp4");
}

TEST(FileUrlTest, TheResultParsesAsAFileUrlWithTheSamePath) {
  const auto url = FileUrlFromPath("/a b/\xC3\xA9");
  ASSERT_TRUE(url.ok());
  const auto parsed = UrlReference::ParseAbsolute(*url);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->scheme, "file");
  EXPECT_EQ(parsed->path, "/a%20b/%C3%A9");
}

TEST(FileUrlTest, RelativeAndEmptyPathsAreRejected) {
  EXPECT_EQ(FileUrlFromPath("movie.mkv").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(FileUrlFromPath("").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(FileUrlTest, ANulByteIsRejected) {
  EXPECT_EQ(FileUrlFromPath(std::string("/a\0b", 4)).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
