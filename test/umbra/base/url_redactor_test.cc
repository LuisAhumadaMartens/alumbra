#include "umbra/base/url_redactor.h"

#include <array>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"

namespace umbra::base {
namespace {

constexpr std::array<std::uint64_t, 2> kKey = {7, 11};

bool Contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

TEST(UrlRedactionTest, PathEmbeddedTokensNeverAppear) {
  const UrlRedactor redactor(kKey);
  for (const std::string url :
       {"https://cdn.example.com/hls/AbC123tokenXyz/seg1.m4s",
        "https://cdn.example.com/a;jsessionid=9f8e7d/seg.m4s",
        "https://cdn.example.com/token/eyJhbGciOiJIUzI1NiJ9.e30.sig/video.m3u8",
        "https://cdn.example.com/%73ecret%2Fpath/seg.ts"}) {
    const std::string output = redactor.RedactUrl(url);
    EXPECT_EQ(output.rfind("https://cdn.example.com [url:", 0), 0U) << url;
    EXPECT_FALSE(Contains(output, "seg")) << output;
    EXPECT_FALSE(Contains(output, "token")) << output;
    EXPECT_FALSE(Contains(output, "jsessionid")) << output;
  }
}

TEST(UrlRedactionTest, QueryValuesFragmentsAndUserInformationNeverAppear) {
  const UrlRedactor redactor(kKey);
  const std::string output = redactor.RedactUrl(
      "https://alice:hunter2@cdn.example.com:8443/v/main.m3u8?hdnts=exp~99&"
      "Policy=abc#t=30");
  EXPECT_EQ(output.rfind("https://cdn.example.com:8443 [url:", 0), 0U);
  for (const std::string secret : {"alice", "hunter2", "hdnts", "exp~99",
                                   "Policy", "abc", "t=30", "main.m3u8"}) {
    EXPECT_FALSE(Contains(output, secret)) << secret;
  }
}

TEST(UrlRedactionTest, DigestIsKeyedPerPlaybackAndCoversTheWholeUrl) {
  const std::string url = "https://cdn.example.com/a?x=1";
  EXPECT_EQ(UrlRedactor(kKey).RedactUrl(url), UrlRedactor(kKey).RedactUrl(url));
  EXPECT_NE(UrlRedactor(kKey).RedactUrl(url),
            UrlRedactor({kKey[0] + 1, kKey[1]}).RedactUrl(url));
  EXPECT_NE(UrlRedactor(kKey).RedactUrl(url),
            UrlRedactor(kKey).RedactUrl(url + "&y=2"));
}

TEST(UrlRedactionTest, AuthCookieAndConfiguredTokenHeadersAreRedactedInFields) {
  const UrlRedactor redactor(kKey, {"X-Edge-Auth"});
  for (const std::string name :
       {"Authorization", "Proxy-Authorization", "Cookie", "Set-Cookie",
        "X-Edge-Auth", "x_edge_auth", "X-Api-Key", "X-Auth-Token"}) {
    EXPECT_EQ(redactor.RedactField(name, "Bearer s3cr3t"), "[redacted]")
        << name;
  }
}

TEST(UrlRedactionTest, HeaderLinesAndPairsInTextAreRedacted) {
  const UrlRedactor redactor(kKey, {"X-Edge-Auth"});
  const std::string output = redactor.RedactText(
      "GET /seg failed\nAuthorization: Bearer s3cr3t-value\n"
      "Cookie: sid=abc; theme=dark\nX-Edge-Auth: edge-secret\n"
      "Accept: */*\npassword=hunter2&retry=3 {\"token\":\"jwt-value\"}");
  for (const std::string secret : {"s3cr3t-value", "sid=abc", "theme=dark",
                                   "edge-secret", "hunter2", "jwt-value"}) {
    EXPECT_FALSE(Contains(output, secret)) << secret << " in " << output;
  }
  EXPECT_TRUE(Contains(output, "Accept: */*"));
  EXPECT_TRUE(Contains(output, "retry=3"));
  EXPECT_TRUE(Contains(output, "Authorization: [redacted]"));
}

TEST(UrlRedactionTest, DrmAndLicenseBodiesAreRedactedInFields) {
  const UrlRedactor redactor(kKey);
  for (const std::string name :
       {"license_request_body", "license_response", "drm_body",
        "licence_challenge", "response_body", "request_headers"}) {
    EXPECT_EQ(redactor.RedactField(name, "AAECAwQF"), "[redacted]") << name;
  }
  EXPECT_EQ(redactor.RedactText("license: AAECAwQF"), "license: [redacted]");
}

TEST(UrlRedactionTest, ProhibitedMaterialStaysRedactedInDebugMode) {
  DebugUrlAllowlist allowlist;
  allowlist.paths = {"/video/main.m3u8"};
  allowlist.query_values = {{"quality", {"hd"}}, {"token", {"abc"}}};
  allowlist.fragments = {"chapter"};
  const UrlRedactor redactor(kKey, {"X-Session"}, UrlDiagnostics::kDebugFullUrl,
                             allowlist);
  const std::string output = redactor.RedactUrl(
      "https://user:pw@cdn.example.com/video/main.m3u8?quality=hd&token=abc"
      "#chapter");
  EXPECT_TRUE(
      Contains(output, "/video/main.m3u8?quality=hd&[redacted]#chapter"))
      << output;
  EXPECT_FALSE(Contains(output, "pw"));
  EXPECT_FALSE(Contains(output, "token=abc"));
  EXPECT_EQ(redactor.RedactField("authorization", "Bearer x"), "[redacted]");
  EXPECT_EQ(redactor.RedactField("x-session", "id"), "[redacted]");
  EXPECT_EQ(redactor.RedactField("license_body", "AAEC"), "[redacted]");
  EXPECT_FALSE(Contains(
      redactor.RedactUrl("https://cdn.example.com/other/secret-token-path"),
      "secret-token-path"));
}

}
}
