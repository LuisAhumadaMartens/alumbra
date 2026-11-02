#include "umbra/source/url.h"

#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace umbra::source {
namespace {

constexpr std::string_view kBase = "http://a/b/c/d;p?q";

using ResolutionCase = std::pair<std::string_view, std::string_view>;

void ExpectResolutions(const std::vector<ResolutionCase>& cases) {
  for (const ResolutionCase& test_case : cases) {
    const auto resolved = ResolveReference(kBase, test_case.first);
    ASSERT_TRUE(resolved.ok()) << test_case.first;
    EXPECT_EQ(resolved->ToString(), test_case.second) << test_case.first;
  }
}

TEST(Rfc3986Test, NormalExamplesFromSectionFiveFourOne) {
  ExpectResolutions({ResolutionCase{"g:h", "g:h"},
                     ResolutionCase{"g", "http://a/b/c/g"},
                     ResolutionCase{"./g", "http://a/b/c/g"},
                     ResolutionCase{"g/", "http://a/b/c/g/"},
                     ResolutionCase{"/g", "http://a/g"},
                     ResolutionCase{"//g", "http://g"},
                     ResolutionCase{"?y", "http://a/b/c/d;p?y"},
                     ResolutionCase{"g?y", "http://a/b/c/g?y"},
                     ResolutionCase{"#s", "http://a/b/c/d;p?q#s"},
                     ResolutionCase{"g#s", "http://a/b/c/g#s"},
                     ResolutionCase{"g?y#s", "http://a/b/c/g?y#s"},
                     ResolutionCase{";x", "http://a/b/c/;x"},
                     ResolutionCase{"g;x", "http://a/b/c/g;x"},
                     ResolutionCase{"g;x?y#s", "http://a/b/c/g;x?y#s"},
                     ResolutionCase{"", "http://a/b/c/d;p?q"},
                     ResolutionCase{".", "http://a/b/c/"},
                     ResolutionCase{"./", "http://a/b/c/"},
                     ResolutionCase{"..", "http://a/b/"},
                     ResolutionCase{"../", "http://a/b/"},
                     ResolutionCase{"../g", "http://a/b/g"},
                     ResolutionCase{"../..", "http://a/"},
                     ResolutionCase{"../../", "http://a/"},
                     ResolutionCase{"../../g", "http://a/g"}});
}

TEST(Rfc3986Test, AbnormalExamplesFromSectionFiveFourTwo) {
  ExpectResolutions({ResolutionCase{"../../../g", "http://a/g"},
                     ResolutionCase{"../../../../g", "http://a/g"},
                     ResolutionCase{"/./g", "http://a/g"},
                     ResolutionCase{"/../g", "http://a/g"},
                     ResolutionCase{"g.", "http://a/b/c/g."},
                     ResolutionCase{".g", "http://a/b/c/.g"},
                     ResolutionCase{"g..", "http://a/b/c/g.."},
                     ResolutionCase{"..g", "http://a/b/c/..g"},
                     ResolutionCase{"./../g", "http://a/b/g"},
                     ResolutionCase{"./g/.", "http://a/b/c/g/"},
                     ResolutionCase{"g/./h", "http://a/b/c/g/h"},
                     ResolutionCase{"g/../h", "http://a/b/c/h"},
                     ResolutionCase{"g;x=1/./y", "http://a/b/c/g;x=1/y"},
                     ResolutionCase{"g;x=1/../y", "http://a/b/c/y"},
                     ResolutionCase{"g?y/./x", "http://a/b/c/g?y/./x"},
                     ResolutionCase{"g?y/../x", "http://a/b/c/g?y/../x"},
                     ResolutionCase{"g#s/./x", "http://a/b/c/g#s/./x"},
                     ResolutionCase{"g#s/../x", "http://a/b/c/g#s/../x"},
                     ResolutionCase{"http:g", "http:g"}});
}

TEST(UrlTest, ParsesEveryComponent) {
  const auto url = UrlReference::Parse(
      "HTTPS://user:pw@Example.COM:8443/a/b.M3U8?x=1&y=2#frag");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(url->scheme, "https");
  EXPECT_EQ(url->userinfo, "user:pw");
  EXPECT_EQ(url->host, "example.com");
  EXPECT_EQ(url->port, "8443");
  EXPECT_EQ(url->path, "/a/b.M3U8");
  EXPECT_EQ(url->query, "x=1&y=2");
  EXPECT_EQ(url->fragment, "frag");
  EXPECT_EQ(url->PathExtension(), "m3u8");
  EXPECT_EQ(url->Origin(), "https://example.com:8443");
}

TEST(UrlTest, DistinguishesAbsentFromEmptyComponents) {
  const auto empty = UrlReference::Parse("http://h/p?#");
  ASSERT_TRUE(empty.ok());
  EXPECT_EQ(empty->query, "");
  EXPECT_EQ(empty->fragment, "");
  const auto absent = UrlReference::Parse("http://h/p");
  ASSERT_TRUE(absent.ok());
  EXPECT_FALSE(absent->query.has_value());
  EXPECT_FALSE(absent->fragment.has_value());
  const auto file = UrlReference::Parse("file:///tmp/a.mp4");
  ASSERT_TRUE(file.ok());
  EXPECT_EQ(file->host, "");
  EXPECT_EQ(file->path, "/tmp/a.mp4");
  EXPECT_EQ(file->ToString(), "file:///tmp/a.mp4");
}

TEST(UrlTest, ParsesIpLiteralHosts) {
  const auto url = UrlReference::Parse("rtsp://[::1]:554/stream");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(url->host, "[::1]");
  EXPECT_EQ(url->port, "554");
}

TEST(UrlTest, LastAtSignEndsUserInformation) {
  const auto url = UrlReference::Parse("rtsp://admin:p@ss@cam.test/x");
  ASSERT_TRUE(url.ok());
  EXPECT_EQ(url->userinfo, "admin:p@ss");
  EXPECT_EQ(url->host, "cam.test");
  EXPECT_FALSE(url->WithoutUserInfo().userinfo.has_value());
  EXPECT_EQ(url->WithoutUserInfo().ToString(), "rtsp://cam.test/x");
}

TEST(UrlTest, RejectsMalformedInput) {
  for (const std::string_view text :
       {"http://host:99999/", "http://host:12a/", "http://ho st/",
        "http://[::1/", "http://[::1]x/", "http://a/%4", "http://a/%zz",
        "1http://x", "://x", "http://a/b c", "http://a/\x01",
        "http://a/\xc3\xa9", "http://a/{x}", "http://a/|", "http://a/\\",
        "ht tp://x", "http://[]/"}) {
    const auto url = UrlReference::Parse(text);
    EXPECT_EQ(url.status().code(), absl::StatusCode::kInvalidArgument) << text;
  }
}

TEST(UrlTest, ParseAbsoluteRequiresAScheme) {
  EXPECT_FALSE(UrlReference::ParseAbsolute("/relative").ok());
  EXPECT_FALSE(UrlReference::ParseAbsolute("").ok());
  EXPECT_TRUE(UrlReference::ParseAbsolute("a:b").ok());
}

TEST(UrlTest, ResolutionNeedsAnAbsoluteBase) {
  EXPECT_FALSE(ResolveReference("/relative", "x").ok());
  EXPECT_FALSE(ResolveReference(kBase, "http://[bad").ok());
}

TEST(UrlTest, ResolutionKeepsTheBaseAuthorityIncludingUserInformation) {
  const auto resolved = ResolveReference("rtsp://u:p@h:554/a/b", "c");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->ToString(), "rtsp://u:p@h:554/a/c");
}

TEST(UrlTest, ResolutionWithoutAuthorityMarksADoubleSlashPath) {
  const auto resolved = ResolveReference("x:/", ".//a");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->path, "//a");
  EXPECT_EQ(resolved->ToString(), "x:/.//a");
  const auto reparsed = UrlReference::Parse(resolved->ToString());
  ASSERT_TRUE(reparsed.ok());
  EXPECT_EQ(reparsed->ToString(), resolved->ToString());
}

TEST(UrlTest, MergeAgainstAnEmptyAuthorityPathAddsARoot) {
  const auto resolved = ResolveReference("http://h", "x/y");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->ToString(), "http://h/x/y");
}

TEST(UrlTest, RemovesDotSegments) {
  EXPECT_EQ(RemoveDotSegments("/a/b/c/./../../g"), "/a/g");
  EXPECT_EQ(RemoveDotSegments("mid/content=5/../6"), "mid/6");
  EXPECT_EQ(RemoveDotSegments(""), "");
  EXPECT_EQ(RemoveDotSegments("/.."), "/");
}

TEST(UrlTest, ToStringRoundTripsParsedUrls) {
  for (const std::string_view text :
       {"https://example.com/a/b?c=d#e", "file:///x", "mailto:a@b.test",
        "http://h:80", "//host/path", "?q", "#f", "a/b/../c"}) {
    const auto url = UrlReference::Parse(text);
    ASSERT_TRUE(url.ok()) << text;
    const auto again = UrlReference::Parse(url->ToString());
    ASSERT_TRUE(again.ok()) << text;
    EXPECT_EQ(*again, *url) << text;
  }
}

TEST(UrlTest, PathExtensionHandlesMissingAndTrailingDots) {
  EXPECT_EQ(UrlReference::Parse("http://h/a/b")->PathExtension(), "");
  EXPECT_EQ(UrlReference::Parse("http://h/a/b.")->PathExtension(), "");
  EXPECT_EQ(UrlReference::Parse("http://h/a.d/b")->PathExtension(), "");
  EXPECT_EQ(UrlReference::Parse("http://h/a/b.mp4?x=.ts")->PathExtension(),
            "mp4");
}
}
}
