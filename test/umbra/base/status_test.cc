#include "umbra/base/status.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/cord.h"
#include "gtest/gtest.h"
#include "umbra/base/sip_hash.h"

namespace umbra::base {
namespace {

TEST(SipHashTest, MatchesPublishedSipHash24Vectors) {
  constexpr std::array<std::pair<int, std::uint64_t>, 10> vectors{
      {{0, 0x726fdb47dd0e0e31ULL},
       {1, 0x74f839c593dc67fdULL},
       {7, 0xab0200f58b01d137ULL},
       {8, 0x93f5f5799a932462ULL},
       {15, 0xa129ca6149be45e5ULL},
       {16, 0x3f2acc7f57c29bdbULL},
       {31, 0x32d892fad841c342ULL},
       {32, 0x7127512f72f27cceULL},
       {62, 0xe51b38608ef25f57ULL},
       {63, 0x958a324ceb064572ULL}}};
  std::string message;
  for (int i = 0; i < 64; ++i) {
    message += static_cast<char>(i);
  }
  for (const auto& [length, digest] : vectors) {
    EXPECT_EQ(
        internal::SipHash24(std::string_view(message).substr(0, length),
                            {0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL}),
        digest)
        << length;
  }
}

TEST(UrlRedactorTest, DropsAllUrlSecretsByDefault) {
  UrlRedactor redactor({1, 2});
  const std::string url =
      "https://user:password@example.com:443/path-embedded-token/"
      "segment?token=query-secret#fragment-secret";
  const auto output = redactor.RedactUrl(url);
  EXPECT_EQ(output.substr(0, 25), "https://example.com:443 [");
  EXPECT_EQ(output.size(), 46);
  for (const auto* secret : {"user", "password", "path-embedded-token",
                             "segment", "query-secret", "fragment-secret"}) {
    EXPECT_EQ(output.find(secret), std::string::npos);
  }
  EXPECT_EQ(output, redactor.RedactUrl(url));
  EXPECT_NE(output, redactor.RedactUrl(url + "x"));
  EXPECT_NE(output, UrlRedactor({3, 4}).RedactUrl(url));
  EXPECT_NE(UrlRedactor{}.RedactUrl(url), UrlRedactor{}.RedactUrl(url));
}

TEST(UrlRedactorTest, EmbeddedAndMalformedUrlsAreRedacted) {
  UrlRedactor redactor({1, 2});
  EXPECT_EQ(redactor.RedactText("plain diagnostic"), "plain diagnostic");
  const auto message = redactor.RedactText(
      "failed HTTPS://user:pass@example.org/secret?auth=hidden#private then "
      "http://example.net/other-token");
  EXPECT_NE(message.find("https://example.org [url:"), std::string::npos);
  EXPECT_NE(message.find("http://example.net [url:"), std::string::npos);
  for (const auto* secret :
       {"user", "pass", "secret", "hidden", "private", "other-token"}) {
    EXPECT_EQ(message.find(secret), std::string::npos);
  }
  for (const auto* url :
       {"file:///secret", "data:secret", "/relative-secret",
        "https://bad\\secret/x", "https://bad%secret/x", "https:///secret",
        "https://host/\nsecret", "https://host:secret/x"}) {
    EXPECT_EQ(redactor.RedactUrl(url).find("secret"), std::string::npos);
  }
  EXPECT_EQ(redactor.RedactUrl("https://user:secret@[::1]:80/a").substr(0, 18),
            "https://[::1]:80 [");
}

TEST(UrlRedactorTest, ProhibitedFieldsRemainProhibitedInDebugMode) {
  DebugUrlAllowlist allowlist;
  allowlist.paths.insert("/video/main.m3u8");
  allowlist.query_values["quality"].insert("hd");
  allowlist.query_values["token"].insert("secret");
  allowlist.fragments.insert("chapter");
  UrlRedactor redactor({1, 2}, {"X-Session"}, UrlDiagnostics::kDebugFullUrl,
                       std::move(allowlist));
  const auto output = redactor.RedactUrl(
      "https://user:secret@host/video/"
      "main.m3u8?quality=hd&token=secret#chapter");
  EXPECT_NE(output.find("/video/main.m3u8?quality=hd&[redacted]#chapter"),
            std::string::npos);
  EXPECT_EQ(output.find("secret"), std::string::npos);
  EXPECT_EQ(output.find("user"), std::string::npos);
  const auto opaque = redactor.RedactUrl(
      "https://host/path-token?unclassified=query-token#fragment-token");
  EXPECT_EQ(opaque.find("token"), std::string::npos);
  for (const auto* field : {"Authorization", "COOKIE", "Set-Cookie",
                            "x-session", "drm_request_body", "license_response",
                            "licence_request", "http_headers", "password"}) {
    EXPECT_EQ(redactor.RedactField(field, "secret"), "[redacted]");
  }
  EXPECT_EQ(redactor.RedactField("segment_url", "/relative-secret")
                .find("relative-secret"),
            std::string::npos);
}

[[nodiscard]] absl::Status Propagate(const absl::Status& status, int& calls) {
  ALUMBRA_RETURN_IF_ERROR(([&] {
    ++calls;
    return status;
  })());
  return absl::OkStatus();
}

[[nodiscard]] absl::StatusOr<std::unique_ptr<int>> MoveOnly(
    bool fail, absl::Status error) {
  if (fail) {
    return error;
  }
  return std::make_unique<int>(42);
}

[[nodiscard]] absl::StatusOr<int> Assign(bool fail, absl::Status error,
                                         int& lhs_calls) {
  ALUMBRA_ASSIGN_OR_RETURN(auto value, MoveOnly(fail, error));
  std::unique_ptr<int> assigned;
  ALUMBRA_ASSIGN_OR_RETURN(assigned, MoveOnly(false, error));
  ALUMBRA_ASSIGN_OR_RETURN(assigned, MoveOnly(false, error));
  int result = 0;
  ALUMBRA_ASSIGN_OR_RETURN((++lhs_calls, result),
                           absl::StatusOr<int>(*value + *assigned));
  return result;
}

TEST(StatusTest, CatalogueDefaultsUseCanonicalCodes) {
  UrlRedactor redactor({1, 2});
  for (const auto& entry : kErrorCatalogue) {
    const auto status = MakeError(entry.code, redactor);
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.code(), absl::StatusCode::kUnknown);
    const auto detail = GetErrorDetail(status);
    ASSERT_TRUE(detail.ok());
    ASSERT_TRUE(detail->has_value());
    EXPECT_EQ((*detail)->category, entry.category);
    EXPECT_EQ((*detail)->severity, entry.severity);
    EXPECT_EQ((*detail)->retryable, entry.retryable);
  }
  EXPECT_EQ(MakeError(ErrorCode::kNetworkTimeout, redactor).code(),
            absl::StatusCode::kUnavailable);
  EXPECT_EQ(MakeError(ErrorCode::kMediaCorrupt, redactor).code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(MakeError(ErrorCode::kCodecUnsupported, redactor).code(),
            absl::StatusCode::kUnimplemented);
}

TEST(StatusTest, PayloadSurvivesCopyMoveAndMacroPropagation) {
  UrlRedactor redactor({1, 2});
  auto cause = std::make_shared<ErrorDetail>();
  cause->code = ErrorCode::kNetworkTimeout;
  cause->category = ErrorCategory::kNetwork;
  cause->context = {
      {"http_status", "503"},
      {"request_url", "https://user:secret@host/path-token?q=value"}};
  const auto original = MakeError(
      ErrorCode::kPlayerInternal, redactor,
      {{"platform_error_code", "-9"}, {"authorization", "secret"}}, cause);
  cause->context["http_status"] = "mutated";
  auto copied = original;
  auto moved = std::move(copied);
  int calls = 0;
  const auto propagated = Propagate(moved, calls);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(original, propagated);
  auto detail = GetErrorDetail(propagated);
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  ASSERT_NE((*detail)->cause, nullptr);
  EXPECT_EQ((*detail)->context.at("platform_error_code"), "-9");
  EXPECT_EQ((*detail)->context.at("authorization"), "[redacted]");
  EXPECT_EQ((*detail)->cause->context.at("http_status"), "503");
  EXPECT_EQ((*detail)->cause->context.at("request_url").find("path-token"),
            std::string::npos);
  int lhs_calls = 0;
  auto failed = Assign(true, original, lhs_calls);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status(), original);
  EXPECT_EQ(lhs_calls, 0);
  auto success = Assign(false, original, lhs_calls);
  ASSERT_TRUE(success.ok());
  EXPECT_EQ(*success, 84);
  EXPECT_EQ(lhs_calls, 1);
  EXPECT_TRUE(Propagate(absl::OkStatus(), calls).ok());
}

TEST(StatusTest, AttachRedactsUrlsInMessagesAndPreservesOtherPayloads) {
  UrlRedactor redactor({1, 2});
  absl::Status input = absl::UnavailableError(
      "request https://user:secret@host/path-token?auth=value#private failed");
  input.SetPayload("other", absl::Cord("opaque"));
  ErrorDetail detail{ErrorCode::kNetworkTimeout,
                     ErrorCategory::kNetwork,
                     ErrorSeverity::kRecoverable,
                     true,
                     {},
                     {}};
  const auto status = AttachErrorDetail(input, detail, redactor);
  EXPECT_EQ(status.code(), input.code());
  EXPECT_EQ(status.GetPayload("other"), input.GetPayload("other"));
  for (const auto* secret :
       {"user", "secret", "path-token", "value", "private"}) {
    EXPECT_EQ(std::string(status.message()).find(secret), std::string::npos);
  }
  EXPECT_FALSE(AttachErrorDetail(absl::OkStatus(), detail, redactor).ok());
  auto absent = GetErrorDetail(absl::OkStatus());
  ASSERT_TRUE(absent.ok());
  EXPECT_FALSE(absent->has_value());
}

TEST(StatusTest, RejectsInvalidMetadataAndUnboundedCauses) {
  UrlRedactor redactor({1, 2});
  ErrorDetail invalid;
  invalid.category = ErrorCategory::kNetwork;
  EXPECT_EQ(
      AttachErrorDetail(absl::InternalError("x"), invalid, redactor).code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(MakeError(static_cast<ErrorCode>(9999), redactor).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor,
                      {{"url-https://secret", "x"}})
                .code(),
            absl::StatusCode::kInvalidArgument);
  std::shared_ptr<ErrorDetail> cause = std::make_shared<ErrorDetail>();
  for (int i = 0; i < 16; ++i) {
    auto parent = std::make_shared<ErrorDetail>();
    parent->cause = cause;
    cause = std::move(parent);
  }
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor, {}, cause).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor,
                      {{"message", std::string(65537, 'x')}})
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(StatusTest, RejectsTruncatedOversizedAndTrailingPayloads) {
  UrlRedactor redactor({1, 2});
  auto status = MakeError(ErrorCode::kNetworkTimeout, redactor, {{"a", "b"}});
  const std::string payload =
      std::string(*status.GetPayload(kErrorDetailPayloadUrl));
  for (std::size_t length = 0; length < payload.size(); ++length) {
    status.SetPayload(kErrorDetailPayloadUrl,
                      absl::Cord(payload.substr(0, length)));
    EXPECT_FALSE(GetErrorDetail(status).ok()) << length;
  }
  for (const auto& bytes :
       {payload + "x", std::string(65537, 'x'), std::string(24, '\xff')}) {
    status.SetPayload(kErrorDetailPayloadUrl, absl::Cord(bytes));
    EXPECT_FALSE(GetErrorDetail(status).ok());
  }
}

TEST(StatusTest, ValidatesEveryMetadataFieldAndDuplicateContextKeys) {
  UrlRedactor redactor({1, 2});
  auto status =
      MakeError(ErrorCode::kNetworkTimeout, redactor, {{"a", "b"}, {"c", "d"}});
  const std::string payload =
      std::string(*status.GetPayload(kErrorDetailPayloadUrl));
  for (std::size_t offset : {0, 4, 8, 12, 16, 40}) {
    auto corrupted = payload;
    corrupted[offset] = '\xff';
    status.SetPayload(kErrorDetailPayloadUrl, absl::Cord(corrupted));
    EXPECT_FALSE(GetErrorDetail(status).ok()) << offset;
  }
  auto duplicate = payload;
  duplicate[34] = 'a';
  status.SetPayload(kErrorDetailPayloadUrl, absl::Cord(duplicate));
  EXPECT_FALSE(GetErrorDetail(status).ok());
}

TEST(StatusTest, AcceptsExactLimitsAndRejectsOnePastThem) {
  UrlRedactor redactor({1, 2});
  const auto maximum = MakeError(ErrorCode::kPlayerInternal, redactor,
                                 {{"message", std::string(65536 - 39, 'x')}});
  ASSERT_EQ(maximum.code(), absl::StatusCode::kInternal);
  ASSERT_TRUE(maximum.GetPayload(kErrorDetailPayloadUrl).has_value());
  EXPECT_EQ(maximum.GetPayload(kErrorDetailPayloadUrl)->size(), 65536);
  EXPECT_TRUE(GetErrorDetail(maximum).ok());
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor,
                      {{"message", std::string(65536 - 38, 'x')}})
                .code(),
            absl::StatusCode::kInvalidArgument);
  std::map<std::string, std::string> context;
  for (int i = 0; i < 64; ++i) {
    context.emplace("field_" + std::to_string(i), "x");
  }
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor, context).code(),
            absl::StatusCode::kInternal);
  context.emplace("extra", "x");
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor, context).code(),
            absl::StatusCode::kInvalidArgument);
  auto cause = std::make_shared<ErrorDetail>();
  for (int i = 0; i < 14; ++i) {
    auto parent = std::make_shared<ErrorDetail>();
    parent->cause = cause;
    cause = std::move(parent);
  }
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor, {}, cause).code(),
            absl::StatusCode::kInternal);
  auto parent = std::make_shared<ErrorDetail>();
  parent->cause = cause;
  EXPECT_EQ(MakeError(ErrorCode::kPlayerInternal, redactor, {}, parent).code(),
            absl::StatusCode::kInvalidArgument);
}
}
}
