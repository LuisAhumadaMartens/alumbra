#include "umbra/source/source_resolver.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_type.h"

namespace umbra::source {
namespace {

class FakeByteStreamProvider final : public penumbra::ByteStreamProvider {
 public:
  explicit FakeByteStreamProvider(std::vector<std::string> schemes)
      : schemes_(std::move(schemes)) {}
  [[nodiscard]] std::vector<std::string> Schemes() const override {
    return schemes_;
  }
  absl::StatusOr<std::unique_ptr<penumbra::ByteStream>> Open(
      penumbra::ByteStreamRequest request,
      std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) override {
    return absl::UnimplementedError(callbacks == nullptr ? "no callbacks"
                                                         : request.url);
  }

 private:
  std::vector<std::string> schemes_;
};

class FakeElementaryProvider final : public penumbra::ElementaryStreamProvider {
 public:
  explicit FakeElementaryProvider(std::vector<std::string> schemes)
      : schemes_(std::move(schemes)) {}
  [[nodiscard]] std::vector<std::string> Schemes() const override {
    return schemes_;
  }
  absl::StatusOr<std::unique_ptr<penumbra::ElementaryStream>> Open(
      penumbra::ElementaryStreamRequest request,
      std::shared_ptr<penumbra::ElementaryStreamCallbacks> callbacks) override {
    return absl::UnimplementedError(callbacks == nullptr ? "no callbacks"
                                                         : request.url);
  }

 private:
  std::vector<std::string> schemes_;
};

class FakeCatalogue final : public penumbra::StreamProviderCatalogue {
 public:
  [[nodiscard]] std::vector<std::shared_ptr<penumbra::ByteStreamProvider>>
  ByteStreamProviders() const override {
    return {std::make_shared<FakeByteStreamProvider>(
        std::vector<std::string>{"http", "https", "file"})};
  }
  [[nodiscard]] std::vector<std::shared_ptr<penumbra::ElementaryStreamProvider>>
  ElementaryStreamProviders() const override {
    return {std::make_shared<FakeElementaryProvider>(
        std::vector<std::string>{"rtsp"})};
  }
};

std::shared_ptr<SchemeRegistry> DesktopRegistry() {
  auto registry = std::make_shared<SchemeRegistry>();
  EXPECT_TRUE(registry
                  ->RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                      std::vector<std::string>{"http", "https", "file"}))
                  .ok());
  return registry;
}

SourceResolver MakeResolver(std::shared_ptr<SchemeRegistry> registry) {
  return SourceResolver(std::move(registry),
                        std::make_shared<const base::UrlRedactor>(
                            std::array<std::uint64_t, 2>{1, 2}));
}

base::ErrorDetail DetailOf(const absl::Status& status) {
  const auto detail = base::GetErrorDetail(status);
  EXPECT_TRUE(detail.ok());
  EXPECT_TRUE(detail->has_value());
  return detail->value_or(base::ErrorDetail());
}

TEST(SchemeRegistryTest, RegistersProvidersAndFindsSchemesCaseInsensitively) {
  SchemeRegistry registry;
  ASSERT_TRUE(registry.RegisterFrom(FakeCatalogue()).ok());
  EXPECT_EQ(registry.Schemes(),
            (std::vector<std::string>{"file", "http", "https", "rtsp"}));
  const SchemeEntry* http = registry.Find("HTTP");
  ASSERT_NE(http, nullptr);
  EXPECT_EQ(http->kind, ProviderKind::kByteStream);
  EXPECT_NE(http->byte_stream, nullptr);
  const SchemeEntry* rtsp = registry.Find("RtSp");
  ASSERT_NE(rtsp, nullptr);
  EXPECT_EQ(rtsp->kind, ProviderKind::kElementaryStream);
  EXPECT_NE(rtsp->elementary_stream, nullptr);
  EXPECT_EQ(registry.Find("rtmp"), nullptr);
}

TEST(SchemeRegistryTest, RejectsInvalidAndDuplicateRegistrations) {
  SchemeRegistry registry;
  EXPECT_EQ(registry.RegisterByteStream(nullptr).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(registry.RegisterElementaryStream(nullptr).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(registry
                .RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                    std::vector<std::string>{}))
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(registry
                .RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                    std::vector<std::string>{"Upper"}))
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(registry
                .RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                    std::vector<std::string>{"a", "a"}))
                .code(),
            absl::StatusCode::kAlreadyExists);
  EXPECT_TRUE(registry.Schemes().empty());
  ASSERT_TRUE(registry
                  .RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                      std::vector<std::string>{"srt"}))
                  .ok());
  EXPECT_EQ(
      registry
          .RegisterElementaryStream(std::make_shared<FakeElementaryProvider>(
              std::vector<std::string>{"rtsp", "srt"}))
          .code(),
      absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(registry.Find("rtsp"), nullptr);
}

TEST(SourceResolverTest, ResolvesRegisteredSchemes) {
  const auto resolver = MakeResolver(DesktopRegistry());
  const auto resolved =
      resolver.Resolve("HTTPS://Example.COM/live/Master.m3u8?token=abc");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->kind, ProviderKind::kByteStream);
  EXPECT_NE(resolved->byte_stream, nullptr);
  EXPECT_EQ(resolved->url.ToString(),
            "https://example.com/live/Master.m3u8?token=abc");
  EXPECT_FALSE(resolved->credential.has_value());
  EXPECT_FALSE(resolved->explicit_type.has_value());
  EXPECT_TRUE(resolver.Resolve("file:///tmp/movie.mp4").ok());
}

TEST(SourceResolverTest, UnsupportedSchemeFailsFastNamingSchemeAndPlatform) {
  const auto resolver = MakeResolver(DesktopRegistry());
  const auto rtmp = resolver.Resolve("rtmp://live.test/app/stream");
  ASSERT_FALSE(rtmp.ok());
  EXPECT_EQ(rtmp.status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_NE(rtmp.status().message().find("'rtmp'"), std::string::npos);
  EXPECT_NE(rtmp.status().message().find("desktop"), std::string::npos);
  const auto detail = DetailOf(rtmp.status());
  EXPECT_EQ(detail.code, base::ErrorCode::kNetworkSchemeUnsupported);
  EXPECT_EQ(detail.context.at("scheme"), "rtmp");
  EXPECT_EQ(detail.context.at("platform"), "desktop");
  const auto* entry = base::FindError(detail.code);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->media_error, 4);

  ResolveOptions web;
  web.platform = "web";
  const auto srt = resolver.Resolve("srt://host:9000", web);
  ASSERT_FALSE(srt.ok());
  EXPECT_EQ(DetailOf(srt.status()).context.at("platform"), "web");
}

TEST(SourceResolverTest, FailedResolutionNeverLeaksUrlSecrets) {
  const auto resolver = MakeResolver(DesktopRegistry());
  const auto failed =
      resolver.Resolve("rtmp://user:hunter2@live.test/app?token=SECRETTOKEN");
  ASSERT_FALSE(failed.ok());
  const auto detail = DetailOf(failed.status());
  std::string everything = std::string(failed.status().message());
  for (const auto& [key, value] : detail.context) {
    everything += key;
    everything += "=";
    everything += value;
  }
  EXPECT_EQ(everything.find("hunter2"), std::string::npos);
  EXPECT_EQ(everything.find("SECRETTOKEN"), std::string::npos);
}

TEST(SourceResolverTest, InvalidUrlsAreRejectedWithTheCatalogueCode) {
  const auto resolver = MakeResolver(DesktopRegistry());
  for (const char* text : {"", "relative/path.mp4", "http://ho st/", "://x"}) {
    const auto result = resolver.Resolve(text);
    ASSERT_FALSE(result.ok()) << text;
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(DetailOf(result.status()).code,
              base::ErrorCode::kNetworkUrlInvalid);
  }
}

TEST(SourceResolverTest, UserInformationBecomesACredentialAndIsStripped) {
  auto registry = DesktopRegistry();
  ASSERT_TRUE(
      registry
          ->RegisterElementaryStream(std::make_shared<FakeElementaryProvider>(
              std::vector<std::string>{"rtsp"}))
          .ok());
  const auto resolver = MakeResolver(registry);
  const auto resolved =
      resolver.Resolve("rtsp://admin:s%40cret@Cam.test:554/live?ch=1");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->kind, ProviderKind::kElementaryStream);
  EXPECT_NE(resolved->elementary_stream, nullptr);
  EXPECT_EQ(resolved->byte_stream, nullptr);
  EXPECT_EQ(resolved->url.ToString(), "rtsp://cam.test:554/live?ch=1");
  ASSERT_TRUE(resolved->credential.has_value());
  EXPECT_EQ(resolved->credential->origin, "rtsp://cam.test:554");
  EXPECT_EQ(resolved->credential->credential.username, "admin");
  EXPECT_EQ(resolved->credential->credential.password, "s@cret");
}

TEST(SourceResolverTest, UserNameWithoutPasswordHasAnEmptyPassword) {
  const auto resolver = MakeResolver(DesktopRegistry());
  const auto resolved = resolver.Resolve("https://token@host.test/a.mp4");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->credential->credential.username, "token");
  EXPECT_EQ(resolved->credential->credential.password, "");
}

TEST(SourceResolverTest,
     WebPoliciesRejectUserInformationPointingToTheCallback) {
  const auto resolver = MakeResolver(DesktopRegistry());
  ResolveOptions web;
  web.platform = "web";
  web.user_info = UserInfoPolicy::kReject;
  const auto result = resolver.Resolve("https://u:p@host.test/a.mp4", web);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("credential callback"),
            std::string::npos);
  EXPECT_EQ(DetailOf(result.status()).code,
            base::ErrorCode::kNetworkUrlInvalid);
  EXPECT_TRUE(resolver.Resolve("https://host.test/a.mp4", web).ok());
}

TEST(SourceResolverTest, MalformedUserInformationEscapeIsRejected) {
  const auto resolver = MakeResolver(DesktopRegistry());
  EXPECT_FALSE(resolver.Resolve("https://u:%zz@host.test/a.mp4").ok());
}

TEST(SourceResolverTest, SubRipFileUsedAsPrimaryMediaIsRejected) {
  const auto resolver = MakeResolver(DesktopRegistry());
  for (const char* url : {"https://host.test/subs/en.srt",
                          "http://host.test/x.SRT?v=1", "file:///tmp/en.srt"}) {
    const auto result = resolver.Resolve(url);
    ASSERT_FALSE(result.ok()) << url;
    EXPECT_NE(result.status().message().find("text track"), std::string::npos);
    EXPECT_NE(result.status().message().find("SubRip"), std::string::npos);
    EXPECT_EQ(DetailOf(result.status()).code,
              base::ErrorCode::kSourceTypeUnsupported);
  }
}

TEST(SourceResolverTest, SrtSchemeIsSecureReliableTransportNotSubRip) {
  auto registry = DesktopRegistry();
  ASSERT_TRUE(registry
                  ->RegisterByteStream(std::make_shared<FakeByteStreamProvider>(
                      std::vector<std::string>{"srt"}))
                  .ok());
  const auto resolver = MakeResolver(registry);
  const auto resolved = resolver.Resolve("srt://relay.test:9000/live.srt");
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->kind, ProviderKind::kByteStream);
}

TEST(SourceResolverTest, ExplicitTypeIsRecordedAndOverridesTheSubRipRule) {
  const auto resolver = MakeResolver(DesktopRegistry());
  ResolveOptions options;
  options.type_hint = SourceType::kHls;
  const auto resolved =
      resolver.Resolve("https://host.test/playlist.srt", options);
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->explicit_type, SourceType::kHls);
}

TEST(SourceResolverTest, ParsesTheTypeHintSetting) {
  const auto automatic = ParseTypeHint("auto");
  ASSERT_TRUE(automatic.ok());
  EXPECT_FALSE(automatic->has_value());
  const auto dash = ParseTypeHint("dash");
  ASSERT_TRUE(dash.ok());
  EXPECT_EQ(*dash, SourceType::kDash);
  EXPECT_EQ(ParseTypeHint("subrip").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseTypeHint("").status().code(),
            absl::StatusCode::kInvalidArgument);
}

}
}
