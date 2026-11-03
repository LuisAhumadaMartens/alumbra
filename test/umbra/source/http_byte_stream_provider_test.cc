#include "umbra/source/http_byte_stream_provider.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "fake_http_transport.h"
#include "gtest/gtest.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"
#include "umbra/source/byte_stream_data_source.h"
#include "umbra/source/credentials.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source {
namespace {

using penumbra::HttpHeader;
using test::FakeHttpTransport;

constexpr std::string_view kUrl = "https://media.example/movie.mp4";

std::vector<uint8_t> Pattern(size_t size) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<uint8_t>((i * 29 + 7) & 0xFF);
  }
  return bytes;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& bytes, size_t offset,
                           size_t size) {
  return {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
          bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)};
}

std::optional<std::string> HeaderValue(const penumbra::HttpRequest& request,
                                       const std::string& name) {
  for (const HttpHeader& header : request.headers) {
    if (header.name == name) {
      return header.value;
    }
  }
  return std::nullopt;
}

class Recorder final : public penumbra::ByteStreamCallbacks {
 public:
  absl::Status OnOpen(penumbra::ByteStreamInfo i) override {
    info = std::move(i);
    return absl::OkStatus();
  }
  absl::Status OnData(penumbra::ByteStreamChunk chunk) override {
    const auto view = chunk.data.span();
    data.insert(data.end(), view.begin(), view.end());
    return absl::OkStatus();
  }
  absl::Status OnEnd() override {
    ended = true;
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status e) override {
    error = std::move(e);
    return absl::OkStatus();
  }

  std::optional<penumbra::ByteStreamInfo> info;
  std::vector<uint8_t> data;
  bool ended = false;
  std::optional<absl::Status> error;
};

class Observer final : public HttpRequestObserver {
 public:
  void OnRequestComplete(const HttpRequestReport& report) override {
    reports.push_back(report);
  }
  std::vector<HttpRequestReport> reports;
};

class Credentials final : public CredentialProvider {
 public:
  void RequestCredential(const CredentialChallenge& challenge,
                         CredentialResponse respond) override {
    challenges.push_back(challenge);
    responders.push_back(std::move(respond));
  }
  void Answer(size_t index, std::optional<penumbra::Credential> credential) {
    std::move(responders[index])(std::move(credential));
  }
  std::vector<CredentialChallenge> challenges;
  std::vector<CredentialResponse> responders;
};

base::ErrorCode CodeOf(const absl::Status& status) {
  const auto detail = base::GetErrorDetail(status);
  EXPECT_TRUE(detail.ok() && detail->has_value());
  return detail->value().code;
}

class HttpProviderTest : public ::testing::Test {
 protected:
  HttpProviderTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        transport_(std::make_shared<FakeHttpTransport>()),
        redactor_(std::make_shared<base::UrlRedactor>()),
        observer_(std::make_shared<Observer>()),
        credentials_(std::make_shared<Credentials>()) {}

  void MakeProvider(HttpByteStreamOptions options = {}) {
    HttpByteStreamDependencies dependencies;
    dependencies.transport = transport_;
    dependencies.runner = runner_;
    dependencies.redactor = redactor_;
    dependencies.credentials = credentials_;
    dependencies.observer = observer_;
    dependencies.clock = &environment_.clock();
    auto provider = HttpByteStreamProvider::Create(std::move(dependencies),
                                                   std::move(options));
    ASSERT_TRUE(provider.ok());
    provider_ = *std::move(provider);
  }

  void SetUp() override { MakeProvider(); }

  void TearDown() override {
    Run([&] { streams_.clear(); });
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    ASSERT_TRUE(runner_->PostTask(std::move(task)));
    environment_.RunUntilIdle();
  }

  std::shared_ptr<Recorder> OpenStream(penumbra::ByteStreamRequest request,
                                       absl::Status* open_status = nullptr) {
    auto recorder = std::make_shared<Recorder>();
    Run([&] {
      auto stream = provider_->Open(std::move(request), recorder);
      if (open_status != nullptr) {
        *open_status = stream.status();
      }
      if (stream.ok()) {
        streams_.push_back(*std::move(stream));
      }
    });
    return recorder;
  }

  std::shared_ptr<Recorder> OpenUrl(uint64_t offset = 0,
                                    std::optional<uint64_t> length = {}) {
    penumbra::ByteStreamRequest request;
    request.url = std::string(kUrl);
    request.offset = offset;
    request.length = length;
    return OpenStream(std::move(request));
  }

  void Settle() { environment_.RunUntilIdle(); }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<FakeHttpTransport> transport_;
  std::shared_ptr<base::UrlRedactor> redactor_;
  std::shared_ptr<Observer> observer_;
  std::shared_ptr<Credentials> credentials_;
  std::shared_ptr<HttpByteStreamProvider> provider_;
  std::vector<std::unique_ptr<penumbra::ByteStream>> streams_;
};

TEST(HttpOptionsFromSettingsTest, DefaultsMatchTheSchemaAndOverridesApply) {
  settings::Store store(settings::GetRegistry(), settings::Platform::kLinux);
  const HttpByteStreamOptions defaults =
      HttpByteStreamOptionsFromSettings(store.TakeSnapshot());
  EXPECT_EQ(defaults.connect_timeout, absl::Milliseconds(10000));
  EXPECT_EQ(defaults.read_timeout, absl::Milliseconds(20000));
  EXPECT_EQ(defaults.max_redirects, 20);
  EXPECT_FALSE(defaults.allow_https_to_http_redirect);
  EXPECT_TRUE(defaults.user_agent.empty());

  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator,
                       settings::kNetworkHttpMaxRedirects, 5)
                  .ok());
  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator,
                       settings::kNetworkHttpConnectTimeoutMs,
                       absl::Milliseconds(2500))
                  .ok());
  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator,
                       settings::kNetworkHttpAllowHttpsToHttpRedirect, true)
                  .ok());
  ASSERT_TRUE(store
                  .Set(settings::Layer::kIntegrator,
                       settings::kNetworkHttpUserAgent, std::string("Embed/1"))
                  .ok());
  const HttpByteStreamOptions custom =
      HttpByteStreamOptionsFromSettings(store.TakeSnapshot());
  EXPECT_EQ(custom.max_redirects, 5);
  EXPECT_EQ(custom.connect_timeout, absl::Milliseconds(2500));
  EXPECT_TRUE(custom.allow_https_to_http_redirect);
  EXPECT_EQ(custom.user_agent, "Embed/1");
  EXPECT_FALSE(store
                   .Set(settings::Layer::kIntegrator,
                        settings::kNetworkHttpMaxRedirects, 21)
                   .ok());
  EXPECT_FALSE(store
                   .Set(settings::Layer::kIntegrator,
                        settings::kNetworkHttpReadTimeoutMs,
                        absl::Milliseconds(999))
                   .ok());
}

TEST_F(HttpProviderTest, ServesHttpAndHttps) {
  EXPECT_EQ(provider_->Schemes(), (std::vector<std::string>{"http", "https"}));
}

TEST_F(HttpProviderTest, MediaRequestsAskForIdentityEncodingAndARange) {
  MakeProvider({.connect_timeout = absl::Seconds(3),
                .read_timeout = absl::Seconds(9),
                .max_redirects = 5,
                .allow_https_to_http_redirect = false,
                .user_agent = "Player/9",
                .headers = {{.name = "X-Custom", .value = "1"}}});
  OpenUrl(1500);
  ASSERT_EQ(transport_->Requests().size(), 1U);
  const penumbra::HttpRequest& request = transport_->Requests()[0]->request;
  EXPECT_EQ(request.url, kUrl);
  EXPECT_EQ(request.method, penumbra::HttpMethod::kGet);
  ASSERT_TRUE(request.range.has_value());
  EXPECT_EQ(request.range->offset, 1500U);
  EXPECT_FALSE(request.range->length.has_value());
  EXPECT_EQ(HeaderValue(request, "Accept-Encoding"), "identity");
  EXPECT_EQ(HeaderValue(request, "User-Agent"), "Player/9");
  EXPECT_EQ(HeaderValue(request, "X-Custom"), "1");
  EXPECT_EQ(request.connect_timeout, absl::Seconds(3));
  EXPECT_EQ(request.redirects.max_redirects, 5);
  EXPECT_FALSE(request.redirects.allow_https_downgrade);
  EXPECT_FALSE(request.include_credentials);
  EXPECT_EQ(request.request_class, penumbra::RequestClass::kMedia);
}

TEST_F(HttpProviderTest, DefaultsFollowTheSchema) {
  OpenUrl();
  const penumbra::HttpRequest& request = transport_->Requests()[0]->request;
  EXPECT_EQ(HeaderValue(request, "User-Agent"), std::string(kDefaultUserAgent));
  EXPECT_EQ(request.connect_timeout, absl::Milliseconds(10000));
  EXPECT_EQ(request.redirects.max_redirects, 20);
  EXPECT_FALSE(request.redirects.allow_https_downgrade);
  ASSERT_TRUE(request.range.has_value());
  EXPECT_EQ(request.range->offset, 0U);
}

TEST_F(HttpProviderTest, ManifestRequestsCarryNoRangeOrEncodingOverride) {
  penumbra::ByteStreamRequest request;
  request.url = std::string(kUrl);
  request.request_class = penumbra::RequestClass::kManifest;
  OpenStream(std::move(request));
  const penumbra::HttpRequest& http = transport_->Requests()[0]->request;
  EXPECT_FALSE(http.range.has_value());
  EXPECT_FALSE(HeaderValue(http, "Accept-Encoding").has_value());
}

TEST_F(HttpProviderTest, APartialResponseReportsSizeSeekabilityAndType) {
  const auto content = Pattern(1000);
  const auto recorder = OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-999/5000"},
                       {.name = "content-type", .value = "video/mp4"}});
  transport_->Chunk(0, Slice(content, 0, 600));
  transport_->Chunk(0, Slice(content, 600, 400));
  transport_->Complete(0, 206, 1000);
  Settle();
  ASSERT_TRUE(recorder->info.has_value());
  EXPECT_TRUE(recorder->info->seekable);
  EXPECT_EQ(recorder->info->size, 5000U);
  EXPECT_EQ(recorder->info->content_type, "video/mp4");
  EXPECT_EQ(recorder->info->final_url, kUrl);
  EXPECT_EQ(recorder->data, content);
  EXPECT_TRUE(recorder->ended);
  EXPECT_FALSE(recorder->error.has_value());
}

TEST_F(HttpProviderTest, ARangeThatStartsElsewhereIsAnError) {
  const auto recorder = OpenUrl(1000);
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-999/5000"}});
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkRequestFailed);
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
}

TEST_F(HttpProviderTest, ServersThatIgnoreRangesAreSkippedToTheOffset) {
  const auto content = Pattern(3000);
  const auto recorder = OpenUrl(1000);
  transport_->Respond(0, 200,
                      {{.name = "Content-Length", .value = "3000"},
                       {.name = "Content-Type", .value = "video/mp4"}});
  transport_->Chunk(0, Slice(content, 0, 700));
  transport_->Chunk(0, Slice(content, 700, 700));
  transport_->Chunk(0, Slice(content, 1400, 1600));
  transport_->Complete(0, 200, 3000);
  Settle();
  ASSERT_TRUE(recorder->info.has_value());
  EXPECT_FALSE(recorder->info->seekable);
  EXPECT_EQ(recorder->info->size, 3000U);
  EXPECT_EQ(recorder->data, Slice(content, 1000, 2000));
  EXPECT_TRUE(recorder->ended);
}

TEST_F(HttpProviderTest, AcceptRangesMakesAFullResponseSeekable) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 200,
                      {{.name = "Content-Length", .value = "10"},
                       {.name = "Accept-Ranges", .value = "bytes"}});
  Settle();
  ASSERT_TRUE(recorder->info.has_value());
  EXPECT_TRUE(recorder->info->seekable);
}

TEST_F(HttpProviderTest, AnOffsetAtTheEndGetsAnEmptyStreamFromA416) {
  const auto recorder = OpenUrl(5000);
  transport_->Respond(0, 416,
                      {{.name = "Content-Range", .value = "bytes */5000"}});
  Settle();
  ASSERT_TRUE(recorder->info.has_value());
  EXPECT_EQ(recorder->info->size, 5000U);
  EXPECT_TRUE(recorder->info->seekable);
  EXPECT_TRUE(recorder->data.empty());
  EXPECT_TRUE(recorder->ended);
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
}

TEST_F(HttpProviderTest, A416InsideTheEntityIsReported) {
  const auto recorder = OpenUrl(100);
  transport_->Respond(0, 416,
                      {{.name = "Content-Range", .value = "bytes */5000"}});
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(recorder->error->code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(CodeOf(*recorder->error),
            base::ErrorCode::kNetworkRangeUnsatisfiable);
}

struct StatusCase {
  int status;
  base::ErrorCode code;
  absl::StatusCode canonical;
};

StatusCase MakeStatusCase(int status, base::ErrorCode code,
                          absl::StatusCode canonical) {
  return {.status = status, .code = code, .canonical = canonical};
}

TEST_F(HttpProviderTest, ErrorStatusesMapToCatalogueCodes) {
  using absl::StatusCode;
  using base::ErrorCode;
  const std::vector<StatusCase> cases = {
      MakeStatusCase(404, ErrorCode::kSourceNotFound, StatusCode::kNotFound),
      MakeStatusCase(410, ErrorCode::kSourceNotFound, StatusCode::kNotFound),
      MakeStatusCase(403, ErrorCode::kNetworkAccessDenied,
                     StatusCode::kPermissionDenied),
      MakeStatusCase(408, ErrorCode::kNetworkServerError,
                     StatusCode::kUnavailable),
      MakeStatusCase(429, ErrorCode::kNetworkServerError,
                     StatusCode::kUnavailable),
      MakeStatusCase(503, ErrorCode::kNetworkServerError,
                     StatusCode::kUnavailable),
      MakeStatusCase(418, ErrorCode::kNetworkRequestFailed,
                     StatusCode::kUnavailable),
      MakeStatusCase(204, ErrorCode::kNetworkRequestFailed,
                     StatusCode::kUnavailable),
  };
  size_t index = 0;
  for (const StatusCase& test_case : cases) {
    const auto recorder = OpenUrl();
    transport_->Respond(index, test_case.status, {});
    Settle();
    ASSERT_TRUE(recorder->error.has_value()) << test_case.status;
    EXPECT_EQ(CodeOf(*recorder->error), test_case.code) << test_case.status;
    EXPECT_EQ(recorder->error->code(), test_case.canonical) << test_case.status;
    EXPECT_FALSE(recorder->info.has_value());
    EXPECT_TRUE(transport_->Requests()[index]->cancelled);
    ++index;
  }
}

TEST_F(HttpProviderTest, ARetryAfterHeaderTravelsInTheErrorContext) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 503, {{.name = "Retry-After", .value = "120"}});
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  const auto detail = base::GetErrorDetail(*recorder->error);
  ASSERT_TRUE(detail.ok() && detail->has_value());
  EXPECT_EQ(detail->value().context.at("retry_after"), "120");
  EXPECT_EQ(detail->value().context.at("http_status"), "503");
  EXPECT_TRUE(detail->value().retryable);
}

struct TransportCase {
  absl::Status status;
  base::ErrorCode code;
};

TransportCase MakeTransportCase(absl::Status status, base::ErrorCode code) {
  return {.status = std::move(status), .code = code};
}

TEST_F(HttpProviderTest, TransportErrorsMapByCanonicalCode) {
  using base::ErrorCode;
  std::vector<TransportCase> cases;
  cases.push_back(MakeTransportCase(absl::DeadlineExceededError("connect"),
                                    ErrorCode::kNetworkTimeout));
  cases.push_back(
      MakeTransportCase(absl::UnauthenticatedError("TLS: self signed"),
                        ErrorCode::kNetworkTlsFailed));
  cases.push_back(
      MakeTransportCase(absl::FailedPreconditionError("redirect refused"),
                        ErrorCode::kNetworkRedirectRefused));
  cases.push_back(MakeTransportCase(absl::UnavailableError("reset"),
                                    ErrorCode::kNetworkRequestFailed));
  size_t index = 0;
  for (const TransportCase& test_case : cases) {
    const auto recorder = OpenUrl();
    transport_->Fail(index, test_case.status);
    Settle();
    ASSERT_TRUE(recorder->error.has_value());
    EXPECT_EQ(CodeOf(*recorder->error), test_case.code);
    ++index;
  }
}

TEST_F(HttpProviderTest, ATlsFailureIsNotRetriedByTheProvider) {
  const auto recorder = OpenUrl();
  transport_->Fail(0, absl::UnauthenticatedError("TLS: certificate"));
  Settle();
  EXPECT_EQ(transport_->Requests().size(), 1U);
  const auto detail = base::GetErrorDetail(*recorder->error);
  ASSERT_TRUE(detail.ok() && detail->has_value());
  EXPECT_FALSE(detail->value().retryable);
}

TEST_F(HttpProviderTest, ATransportStartFailureIsReturnedFromOpen) {
  transport_->FailNextStart(absl::FailedPreconditionError("stopped"));
  absl::Status status;
  penumbra::ByteStreamRequest request;
  request.url = std::string(kUrl);
  OpenStream(std::move(request), &status);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(CodeOf(status), base::ErrorCode::kNetworkRedirectRefused);
}

TEST_F(HttpProviderTest, OpenRejectsUnsuitableRequests) {
  absl::Status status;
  penumbra::ByteStreamRequest request;
  request.url = "ftp://media.example/a";
  OpenStream(std::move(request), &status);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  penumbra::ByteStreamRequest with_user;
  with_user.url = "https://user:pw@media.example/a";
  OpenStream(std::move(with_user), &status);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  penumbra::ByteStreamRequest relative;
  relative.url = "/a";
  OpenStream(std::move(relative), &status);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(transport_->Requests().empty());
  Run([&] {
    EXPECT_EQ(provider_->Open({}, nullptr).status().code(),
              absl::StatusCode::kInvalidArgument);
  });
}

TEST_F(HttpProviderTest, CreateValidatesItsArguments) {
  HttpByteStreamDependencies dependencies;
  EXPECT_FALSE(HttpByteStreamProvider::Create(dependencies, {}).ok());
  dependencies.transport = transport_;
  dependencies.runner = runner_;
  dependencies.redactor = redactor_;
  EXPECT_TRUE(HttpByteStreamProvider::Create(dependencies, {}).ok());
  HttpByteStreamOptions options;
  options.connect_timeout = absl::ZeroDuration();
  EXPECT_FALSE(HttpByteStreamProvider::Create(dependencies, options).ok());
  options = {};
  options.read_timeout = absl::Seconds(-1);
  EXPECT_FALSE(HttpByteStreamProvider::Create(dependencies, options).ok());
  options = {};
  options.max_redirects = 21;
  EXPECT_FALSE(HttpByteStreamProvider::Create(dependencies, options).ok());
  options.max_redirects = -1;
  EXPECT_FALSE(HttpByteStreamProvider::Create(dependencies, options).ok());
}

TEST_F(HttpProviderTest, ALengthLimitEndsTheStreamEarly) {
  const auto content = Pattern(1000);
  const auto recorder = OpenUrl(0, 300);
  ASSERT_TRUE(transport_->Requests()[0]->request.range.has_value());
  EXPECT_EQ(transport_->Requests()[0]->request.range->length, 300U);
  transport_->Respond(0, 200, {{.name = "Content-Length", .value = "1000"}});
  transport_->Chunk(0, Slice(content, 0, 200));
  transport_->Chunk(0, Slice(content, 200, 400));
  Settle();
  EXPECT_EQ(recorder->data, Slice(content, 0, 300));
  EXPECT_TRUE(recorder->ended);
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
}

TEST_F(HttpProviderTest, TheIdleTimerFiresAfterConnectPlusReadTimeout) {
  MakeProvider({.connect_timeout = absl::Seconds(2),
                .read_timeout = absl::Seconds(5),
                .max_redirects = 20,
                .allow_https_to_http_redirect = false,
                .user_agent = "",
                .headers = {}});
  const auto recorder = OpenUrl();
  environment_.FastForwardBy(absl::Milliseconds(6900));
  EXPECT_FALSE(recorder->error.has_value());
  environment_.FastForwardBy(absl::Milliseconds(200));
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkTimeout);
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
}

TEST_F(HttpProviderTest, EachChunkRestartsTheIdleTimer) {
  MakeProvider({.connect_timeout = absl::Seconds(2),
                .read_timeout = absl::Seconds(5),
                .max_redirects = 20,
                .allow_https_to_http_redirect = false,
                .user_agent = "",
                .headers = {}});
  const auto recorder = OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-99/100"}});
  Settle();
  for (int i = 0; i < 4; ++i) {
    environment_.FastForwardBy(absl::Seconds(4));
    transport_->Chunk(0, {1, 2, 3});
    Settle();
    EXPECT_FALSE(recorder->error.has_value());
  }
  environment_.FastForwardBy(absl::Seconds(6));
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkTimeout);
}

TEST_F(HttpProviderTest, APausedStreamNeverTimesOutAndResumeRearmsTheTimer) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-99/100"}});
  Settle();
  Run([&] { EXPECT_TRUE(streams_[0]->Pause().ok()); });
  EXPECT_TRUE(transport_->Requests()[0]->paused);
  environment_.FastForwardBy(absl::Hours(1));
  EXPECT_FALSE(recorder->error.has_value());
  Run([&] { EXPECT_TRUE(streams_[0]->Resume().ok()); });
  EXPECT_FALSE(transport_->Requests()[0]->paused);
  environment_.FastForwardBy(absl::Seconds(19));
  EXPECT_FALSE(recorder->error.has_value());
  environment_.FastForwardBy(absl::Seconds(2));
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkTimeout);
}

TEST_F(HttpProviderTest, PauseAndResumeOnAFinishedStreamFail) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-9/10"}});
  transport_->Complete(0, 206, 0);
  Settle();
  ASSERT_TRUE(recorder->ended);
  Run([&] {
    EXPECT_EQ(streams_[0]->Pause().code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(streams_[0]->Resume().code(),
              absl::StatusCode::kFailedPrecondition);
  });
}

TEST_F(HttpProviderTest, PausedIntervalsAreExcludedFromTheTransferDuration) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-99/100"}});
  Settle();
  Run([&] { EXPECT_TRUE(streams_[0]->Pause().ok()); });
  environment_.clock().Advance(absl::Milliseconds(100));
  Run([&] { EXPECT_TRUE(streams_[0]->Resume().ok()); });
  transport_->Complete(0, 206, 100);
  Settle();
  ASSERT_EQ(observer_->reports.size(), 1U);
  const HttpRequestReport& report = observer_->reports[0];
  EXPECT_EQ(report.paused_duration, absl::Milliseconds(100));
  EXPECT_EQ(report.transfer_duration, absl::Milliseconds(200));
  EXPECT_EQ(report.time_to_first_byte, absl::Milliseconds(50));
  EXPECT_EQ(report.body_bytes, 100U);
  EXPECT_EQ(report.status, 206);
  EXPECT_EQ(report.protocol, penumbra::HttpProtocol::kHttp2);
  EXPECT_EQ(report.url, kUrl);
  EXPECT_FALSE(report.coarse_timing);
}

TEST_F(HttpProviderTest, CoarseTimingIsPassedToTheObserver) {
  OpenUrl();
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-9/10"}});
  transport_->Complete(0, 206, 10, true);
  Settle();
  ASSERT_EQ(observer_->reports.size(), 1U);
  EXPECT_TRUE(observer_->reports[0].coarse_timing);
}

TEST_F(HttpProviderTest, CancelDropsLaterCallbacksAndIsIdempotent) {
  const auto recorder = OpenUrl();
  Run([&] {
    streams_[0]->Cancel();
    streams_[0]->Cancel();
  });
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
  transport_->Respond(0, 206,
                      {{.name = "Content-Range", .value = "bytes 0-9/10"}});
  transport_->Chunk(0, {1});
  transport_->Complete(0, 206, 1);
  Settle();
  EXPECT_FALSE(recorder->info.has_value());
  EXPECT_TRUE(recorder->data.empty());
  EXPECT_FALSE(recorder->ended);
  EXPECT_TRUE(observer_->reports.empty());
}

TEST_F(HttpProviderTest, DestroyingTheStreamCancelsTheRequest) {
  const auto recorder = OpenUrl();
  Run([&] { streams_.clear(); });
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
  transport_->Chunk(0, {1});
  Settle();
  EXPECT_TRUE(recorder->data.empty());
}

TEST_F(HttpProviderTest, ABasicChallengeAsksTheCredentialProviderAndRetries) {
  const auto recorder = OpenUrl();
  transport_->Respond(
      0, 401,
      {{.name = "WWW-Authenticate",
        .value = R"(Basic realm="Private Area", charset="UTF-8")"}});
  Settle();
  ASSERT_EQ(credentials_->challenges.size(), 1U);
  EXPECT_EQ(credentials_->challenges[0].origin, "https://media.example");
  EXPECT_EQ(credentials_->challenges[0].scheme, "Basic");
  EXPECT_EQ(credentials_->challenges[0].realm, "Private Area");
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
  EXPECT_FALSE(recorder->error.has_value());
  credentials_->Answer(
      0, penumbra::Credential{.username = "user", .password = "pass"});
  Settle();
  ASSERT_EQ(transport_->Requests().size(), 2U);
  EXPECT_EQ(HeaderValue(transport_->Requests()[1]->request, "Authorization"),
            "Basic dXNlcjpwYXNz");
  transport_->Respond(1, 206,
                      {{.name = "Content-Range", .value = "bytes 0-9/10"}});
  transport_->Chunk(1, {9, 8, 7});
  transport_->Complete(1, 206, 3);
  Settle();
  ASSERT_TRUE(recorder->info.has_value());
  EXPECT_EQ(recorder->data, (std::vector<uint8_t>{9, 8, 7}));
  EXPECT_TRUE(recorder->ended);
}

TEST_F(HttpProviderTest, ASuccessfulCredentialIsRememberedForTheOrigin) {
  OpenUrl();
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  credentials_->Answer(0,
                       penumbra::Credential{.username = "u", .password = "p"});
  Settle();
  transport_->Respond(1, 206,
                      {{.name = "Content-Range", .value = "bytes 0-9/10"}});
  Settle();
  OpenUrl(4);
  ASSERT_EQ(transport_->Requests().size(), 3U);
  EXPECT_EQ(HeaderValue(transport_->Requests()[2]->request, "Authorization"),
            "Basic dTpw");
  EXPECT_EQ(credentials_->challenges.size(), 1U);
}

TEST_F(HttpProviderTest, ACredentialFromTheUrlIsUsedWithoutAskingTheProvider) {
  penumbra::ByteStreamRequest request;
  request.url = std::string(kUrl);
  request.credential =
      penumbra::Credential{.username = "cam", .password = "s3"};
  OpenStream(std::move(request));
  EXPECT_FALSE(HeaderValue(transport_->Requests()[0]->request, "Authorization")
                   .has_value());
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="cams")"}});
  Settle();
  EXPECT_TRUE(credentials_->challenges.empty());
  ASSERT_EQ(transport_->Requests().size(), 2U);
  EXPECT_EQ(HeaderValue(transport_->Requests()[1]->request, "Authorization"),
            "Basic Y2FtOnMz");
}

TEST_F(HttpProviderTest,
       RejectedCredentialsFailWithAccessDeniedAndAreForgotten) {
  const auto recorder = OpenUrl();
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  credentials_->Answer(
      0, penumbra::Credential{.username = "u", .password = "bad"});
  Settle();
  transport_->Respond(
      1, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkAccessDenied);
  EXPECT_EQ(transport_->Requests().size(), 2U);
  EXPECT_EQ(credentials_->challenges.size(), 1U);
}

TEST_F(HttpProviderTest, NoCredentialMeansAccessDenied) {
  const auto recorder = OpenUrl();
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  credentials_->Answer(0, std::nullopt);
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkAccessDenied);
  EXPECT_EQ(recorder->error->code(), absl::StatusCode::kPermissionDenied);
}

TEST_F(HttpProviderTest, UnsupportedSchemesAreDeniedWithoutAskingAnyone) {
  const auto recorder = OpenUrl();
  transport_->Respond(0, 401,
                      {{.name = "WWW-Authenticate",
                        .value = R"(Digest realm="r", nonce="n")"}});
  Settle();
  EXPECT_TRUE(credentials_->challenges.empty());
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkAccessDenied);
}

TEST_F(HttpProviderTest, BasicIsFoundAmongSeveralChallenges) {
  OpenUrl();
  transport_->Respond(
      0, 401,
      {{.name = "WWW-Authenticate", .value = R"(Digest realm="d", nonce="n")"},
       {.name = "WWW-Authenticate", .value = R"(Basic realm="b")"}});
  Settle();
  ASSERT_EQ(credentials_->challenges.size(), 1U);
  EXPECT_EQ(credentials_->challenges[0].realm, "b");
}

TEST_F(HttpProviderTest, WithoutACredentialProviderAChallengeIsDenied) {
  HttpByteStreamDependencies dependencies;
  dependencies.transport = transport_;
  dependencies.runner = runner_;
  dependencies.redactor = redactor_;
  dependencies.clock = &environment_.clock();
  auto provider = HttpByteStreamProvider::Create(dependencies, {});
  ASSERT_TRUE(provider.ok());
  provider_ = *std::move(provider);
  const auto recorder = OpenUrl();
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  ASSERT_TRUE(recorder->error.has_value());
  EXPECT_EQ(CodeOf(*recorder->error), base::ErrorCode::kNetworkAccessDenied);
}

TEST_F(HttpProviderTest, CancellingWhileAwaitingACredentialIgnoresTheAnswer) {
  const auto recorder = OpenUrl();
  transport_->Respond(
      0, 401, {{.name = "WWW-Authenticate", .value = R"(Basic realm="r")"}});
  Settle();
  Run([&] { streams_[0]->Cancel(); });
  credentials_->Answer(0,
                       penumbra::Credential{.username = "u", .password = "p"});
  Settle();
  EXPECT_EQ(transport_->Requests().size(), 1U);
  EXPECT_FALSE(recorder->error.has_value());
}

class HttpDataSourceTest : public HttpProviderTest {
 protected:
  void OpenSource() {
    Run([&] {
      penumbra::ByteStreamRequest request;
      request.url = std::string(kUrl);
      open_request_ = ByteStreamDataSource::Open(
          provider_, std::move(request), runner_, redactor_, {},
          [this](absl::StatusOr<std::shared_ptr<DataSource>> result) {
            open_result_ = std::move(result);
          });
    });
  }

  std::shared_ptr<ReadRequest> StartRead(uint64_t offset, size_t size) {
    std::shared_ptr<ReadRequest> request;
    Run([&] {
      request = open_result_->value()->Read(
          offset, size, [this](absl::StatusOr<penumbra::SharedBytes> r) {
            results_.push_back(std::move(r));
          });
    });
    return request;
  }

  void TearDown() override {
    Run([&] {
      if (open_result_.has_value() && open_result_->ok()) {
        open_result_->value()->Close();
      }
      open_result_.reset();
    });
    HttpProviderTest::TearDown();
  }

  std::shared_ptr<OpenRequest> open_request_;
  std::optional<absl::StatusOr<std::shared_ptr<DataSource>>> open_result_;
  std::vector<absl::StatusOr<penumbra::SharedBytes>> results_;
};

TEST_F(HttpDataSourceTest, SeekingIssuesANewRangeRequestFromTheOffset) {
  const auto content = Pattern(100000);
  OpenSource();
  transport_->Respond(
      0, 206,
      {{.name = "Content-Range", .value = "bytes 0-99999/100000"},
       {.name = "Content-Type", .value = "video/mp4"}});
  Settle();
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_TRUE(open_result_->ok());
  const DataSourceInfo& info = open_result_->value()->Info();
  EXPECT_TRUE(info.seekable);
  EXPECT_EQ(info.size, 100000U);
  EXPECT_EQ(info.content_type, "video/mp4");
  EXPECT_EQ(info.final_url, kUrl);

  StartRead(0, 1000);
  transport_->Chunk(0, Slice(content, 0, 4000));
  Settle();
  ASSERT_EQ(results_.size(), 1U);
  ASSERT_TRUE(results_[0].ok());

  StartRead(90000, 1000);
  ASSERT_EQ(transport_->Requests().size(), 2U);
  const penumbra::HttpRequest& seek = transport_->Requests()[1]->request;
  ASSERT_TRUE(seek.range.has_value());
  EXPECT_EQ(seek.range->offset, 90000U);
  EXPECT_TRUE(transport_->Requests()[0]->cancelled);
  transport_->Respond(
      1, 206, {{.name = "Content-Range", .value = "bytes 90000-99999/100000"}});
  transport_->Chunk(1, Slice(content, 90000, 2000));
  Settle();
  ASSERT_EQ(results_.size(), 2U);
  ASSERT_TRUE(results_[1].ok());
  const auto view = results_[1]->span();
  EXPECT_EQ(std::vector<uint8_t>(view.begin(), view.end()),
            Slice(content, 90000, 1000));
}

TEST_F(HttpDataSourceTest,
       TheTransportPausesWhenTheQueueFillsAndResumesWhenRead) {
  const auto content = Pattern(2000000);
  OpenSource();
  transport_->Respond(
      0, 206, {{.name = "Content-Range", .value = "bytes 0-1999999/2000000"}});
  Settle();
  ASSERT_TRUE(open_result_->ok());
  for (size_t i = 0; i < 8; ++i) {
    transport_->Chunk(0, Slice(content, i * 65536, 65536));
    Settle();
  }
  EXPECT_TRUE(transport_->Requests()[0]->paused);
  EXPECT_EQ(transport_->Requests()[0]->pauses, 1);
  StartRead(0, 400000);
  EXPECT_FALSE(transport_->Requests()[0]->paused);
  EXPECT_EQ(transport_->Requests()[0]->resumes, 1);
}

TEST_F(HttpDataSourceTest, ATimeoutBeforeTheResponseFailsTheOpen) {
  OpenSource();
  environment_.FastForwardBy(absl::Seconds(31));
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(CodeOf(open_result_->status()), base::ErrorCode::kNetworkTimeout);
}

TEST_F(HttpDataSourceTest, A404FailsTheOpenWithNotFound) {
  OpenSource();
  transport_->Respond(0, 404, {});
  Settle();
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(open_result_->status().code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(CodeOf(open_result_->status()), base::ErrorCode::kSourceNotFound);
}

}
}
