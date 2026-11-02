#include "penumbra/desktop/curl_http_transport.h"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "http_test_server.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/byte_stream_data_source.h"
#include "umbra/source/credentials.h"
#include "umbra/source/data_source.h"
#include "umbra/source/http_byte_stream_provider.h"
#include "umbra/source/read_request.h"

namespace penumbra::desktop {
namespace {

using test::HttpTestServer;
using test::ServerConnection;
using test::ServerHeader;
using test::ServerRequest;

namespace fs = std::filesystem;

constexpr size_t kMebibyte = 1048576;

std::string Pattern(size_t size) {
  std::string bytes(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<char>((i * 31 + 17) & 0xFF);
  }
  return bytes;
}

std::string_view Header(const ServerRequest& request, const std::string& name) {
  const auto found = request.headers.find(name);
  return found == request.headers.end() ? std::string_view()
                                        : std::string_view(found->second);
}

void WriteBody(ServerConnection& connection, std::string_view body) {
  constexpr size_t kPiece = 16384;
  for (size_t position = 0; position < body.size() && connection.Open();
       position += kPiece) {
    connection.Write(body.substr(position, kPiece));
  }
}

test::ServerHandler FileHandler(std::string content, bool ranges) {
  return [content = std::move(content), ranges](ServerConnection& connection,
                                                const ServerRequest& request) {
    const std::string_view range = Header(request, "range");
    std::vector<ServerHeader> headers = {
        {.name = "Content-Type", .value = "video/mp4"}};
    if (ranges) {
      headers.push_back({.name = "Accept-Ranges", .value = "bytes"});
    }
    if (!ranges || !range.starts_with("bytes=")) {
      headers.push_back(
          {.name = "Content-Length", .value = absl::StrCat(content.size())});
      test::SendHead(connection, 200, headers);
      WriteBody(connection, content);
      return;
    }
    const std::string_view spec = range.substr(6);
    const size_t dash = spec.find('-');
    const size_t first = std::stoull(std::string(spec.substr(0, dash)));
    size_t last = content.size() - 1;
    if (dash + 1 < spec.size()) {
      last = std::min<size_t>(last,
                              std::stoull(std::string(spec.substr(dash + 1))));
    }
    if (first >= content.size()) {
      test::SendResponse(connection, 416,
                         {{.name = "Content-Range",
                           .value = absl::StrCat("bytes */", content.size())}},
                         "");
      return;
    }
    headers.push_back({.name = "Content-Range",
                       .value = absl::StrCat("bytes ", first, "-", last, "/",
                                             content.size())});
    headers.push_back(
        {.name = "Content-Length", .value = absl::StrCat(last - first + 1)});
    test::SendHead(connection, 206, headers);
    WriteBody(connection,
              std::string_view(content).substr(first, last - first + 1));
  };
}

bool Poll(const std::function<bool()>& done) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

class Collector final : public HttpTransportCallbacks {
 public:
  absl::Status OnResponse(HttpResponseHead h) override {
    const std::scoped_lock lock(mutex_);
    head = std::move(h);
    events.emplace_back("head");
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnChunk(HttpChunk chunk) override {
    const std::scoped_lock lock(mutex_);
    const auto view = chunk.data.span();
    body.append(reinterpret_cast<const char*>(view.data()), view.size());
    arrivals.push_back(chunk.arrival_time);
    events.emplace_back("chunk");
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnComplete(HttpMetrics m) override {
    const std::scoped_lock lock(mutex_);
    metrics = std::move(m);
    events.emplace_back("complete");
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status e) override {
    const std::scoped_lock lock(mutex_);
    error = std::move(e);
    events.emplace_back("error");
    wake_.notify_all();
    return absl::OkStatus();
  }

  bool WaitFor(const std::function<bool()>& done,
               std::chrono::milliseconds timeout = std::chrono::seconds(30)) {
    std::unique_lock lock(mutex_);
    return wake_.wait_for(lock, timeout, done);
  }
  bool WaitTerminal() {
    return WaitFor([this] { return metrics.has_value() || error.has_value(); });
  }
  size_t BodySize() {
    const std::scoped_lock lock(mutex_);
    return body.size();
  }
  size_t EventCount() {
    const std::scoped_lock lock(mutex_);
    return events.size();
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<HttpResponseHead> head;
  std::string body;
  std::vector<absl::Duration> arrivals;
  std::optional<HttpMetrics> metrics;
  std::optional<absl::Status> error;
  std::vector<std::string> events;
};

class CurlTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto transport = CurlHttpTransport::Create();
    ASSERT_TRUE(transport.ok());
    transport_ = *std::move(transport);
  }

  void TearDown() override {
    handles_.clear();
    if (server_ != nullptr) {
      server_->Stop();
    }
  }

  void StartServer(test::ServerHandler handler, bool tls = false) {
    auto server = HttpTestServer::Start(tls, std::move(handler));
    ASSERT_TRUE(server.ok());
    server_ = *std::move(server);
  }

  static HttpRequest Get(const std::string& url) {
    HttpRequest request;
    request.url = url;
    return request;
  }

  std::shared_ptr<Collector> Fetch(HttpRequest request,
                                   CurlHttpTransport* transport = nullptr) {
    auto collector = std::make_shared<Collector>();
    auto handle = (transport != nullptr ? transport : transport_.get())
                      ->Start(std::move(request), collector);
    EXPECT_TRUE(handle.ok());
    if (handle.ok()) {
      handles_.push_back(*std::move(handle));
      EXPECT_TRUE(collector->WaitTerminal());
    }
    return collector;
  }

  std::shared_ptr<CurlHttpTransport> TrustingTransport() {
    const fs::path path =
        fs::temp_directory_path() /
        absl::StrCat("alumbra_curl_test_ca_", server_->Port(), ".pem");
    {
      std::ofstream out(path);
      out << server_->CertificatePem();
    }
    auto transport =
        CurlHttpTransport::Create({.ca_bundle_path = path.string()});
    EXPECT_TRUE(transport.ok());
    return *std::move(transport);
  }

  std::shared_ptr<CurlHttpTransport> transport_;
  std::unique_ptr<HttpTestServer> server_;
  std::vector<std::unique_ptr<HttpRequestHandle>> handles_;
};

TEST_F(CurlTransportTest, TheBuildHasOpenSslThreeAndHttp2) {
  const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
  ASSERT_NE(info, nullptr);
  EXPECT_NE(info->features & CURL_VERSION_HTTP2, 0U);
  ASSERT_NE(info->ssl_version, nullptr);
  EXPECT_NE(std::string(info->ssl_version).find("OpenSSL/3"), std::string::npos)
      << info->ssl_version;
}

TEST_F(CurlTransportTest, DeliversTheHeadChunksAndOrderedMetrics) {
  const std::string content = Pattern(300000);
  StartServer(FileHandler(content, false));
  const auto collector = Fetch(Get(server_->BaseUrl() + "/media.mp4"));
  ASSERT_TRUE(collector->metrics.has_value());
  ASSERT_TRUE(collector->head.has_value());
  EXPECT_EQ(collector->head->status, 200);
  EXPECT_EQ(collector->head->protocol, HttpProtocol::kHttp1_1);
  EXPECT_EQ(collector->head->final_url, server_->BaseUrl() + "/media.mp4");
  bool found_type = false;
  for (const HttpHeader& header : collector->head->headers) {
    found_type = found_type ||
                 (header.name == "Content-Type" && header.value == "video/mp4");
  }
  EXPECT_TRUE(found_type);
  EXPECT_EQ(collector->body, content);
  EXPECT_EQ(collector->events.front(), "head");
  EXPECT_EQ(collector->events.back(), "complete");
  EXPECT_GT(collector->arrivals.size(), 1U);
  for (size_t i = 1; i < collector->arrivals.size(); ++i) {
    EXPECT_GE(collector->arrivals[i], collector->arrivals[i - 1]);
  }
  const HttpMetrics& metrics = *collector->metrics;
  EXPECT_EQ(metrics.status, 200);
  EXPECT_EQ(metrics.body_bytes, content.size());
  EXPECT_FALSE(metrics.coarse_timing);
  EXPECT_LE(metrics.request_start, metrics.response_start);
  EXPECT_LE(metrics.response_start, collector->arrivals.front());
  EXPECT_LE(collector->arrivals.back(), metrics.completion);
  EXPECT_EQ(metrics.final_url, server_->BaseUrl() + "/media.mp4");
}

TEST_F(CurlTransportTest, SendsTheRequestHeadersAndTheRangeAsGiven) {
  StartServer(FileHandler(Pattern(5000), true));
  HttpRequest request = Get(server_->BaseUrl() + "/media.mp4");
  request.headers = {{.name = "User-Agent", .value = "Player/9"},
                     {.name = "Accept-Encoding", .value = "identity"},
                     {.name = "X-Custom", .value = "abc"}};
  request.range = ByteRange{.offset = 1000, .length = 500};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  const auto requests = server_->Requests();
  ASSERT_EQ(requests.size(), 1U);
  EXPECT_EQ(Header(requests[0], "user-agent"), "Player/9");
  EXPECT_EQ(Header(requests[0], "accept-encoding"), "identity");
  EXPECT_EQ(Header(requests[0], "x-custom"), "abc");
  EXPECT_EQ(Header(requests[0], "range"), "bytes=1000-1499");
  EXPECT_EQ(collector->head->status, 206);
  EXPECT_EQ(collector->body, Pattern(5000).substr(1000, 500));
}

TEST_F(CurlTransportTest, AnOpenEndedRangeAsksForTheRest) {
  StartServer(FileHandler(Pattern(5000), true));
  HttpRequest request = Get(server_->BaseUrl() + "/media.mp4");
  request.range = ByteRange{.offset = 4000, .length = std::nullopt};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(Header(server_->Requests()[0], "range"), "bytes=4000-");
  EXPECT_EQ(collector->body, Pattern(5000).substr(4000));
}

TEST_F(CurlTransportTest, AServerThatIgnoresRangesAnswers200) {
  StartServer(FileHandler(Pattern(5000), false));
  HttpRequest request = Get(server_->BaseUrl() + "/media.mp4");
  request.range = ByteRange{.offset = 1000, .length = std::nullopt};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->head->status, 200);
  EXPECT_EQ(collector->body.size(), 5000U);
}

TEST_F(CurlTransportTest, AnUnsatisfiableRangeIsAResponseNotAnError) {
  StartServer(FileHandler(Pattern(5000), true));
  HttpRequest request = Get(server_->BaseUrl() + "/media.mp4");
  request.range = ByteRange{.offset = 9000, .length = std::nullopt};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  ASSERT_TRUE(collector->head.has_value());
  EXPECT_EQ(collector->head->status, 416);
  EXPECT_EQ(collector->metrics->status, 416);
}

TEST_F(CurlTransportTest, ErrorStatusesAndChallengesReachTheHead) {
  StartServer([](ServerConnection& connection, const ServerRequest& request) {
    if (Header(request, "authorization") == "Basic dXNlcjpwYXNz") {
      test::SendResponse(connection, 200, {}, "welcome");
    } else {
      test::SendResponse(
          connection, 401,
          {{.name = "WWW-Authenticate", .value = R"(Basic realm="Private")"}},
          "no");
    }
  });
  const auto denied = Fetch(Get(server_->BaseUrl() + "/secret"));
  ASSERT_TRUE(denied->metrics.has_value());
  EXPECT_EQ(denied->head->status, 401);
  bool challenge = false;
  for (const HttpHeader& header : denied->head->headers) {
    challenge = challenge || header.name == "WWW-Authenticate";
  }
  EXPECT_TRUE(challenge);
  HttpRequest request = Get(server_->BaseUrl() + "/secret");
  request.headers = {{.name = "Authorization", .value = "Basic dXNlcjpwYXNz"}};
  const auto granted = Fetch(std::move(request));
  ASSERT_TRUE(granted->metrics.has_value());
  EXPECT_EQ(granted->head->status, 200);
  EXPECT_EQ(granted->body, "welcome");
}

TEST_F(CurlTransportTest, PostsABody) {
  StartServer([](ServerConnection& connection, const ServerRequest& request) {
    test::SendResponse(connection, 200, {},
                       absl::StrCat(request.method, ":", request.body));
  });
  HttpRequest request = Get(server_->BaseUrl() + "/license");
  request.method = HttpMethod::kPost;
  request.body = SharedBytes::Copy(std::vector<uint8_t>{'a', 'b', 'c'});
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->body, "POST:abc");
}

test::ServerHandler RedirectChain(const std::string& final_body) {
  const auto body = std::make_shared<const std::string>(final_body);
  return [body](ServerConnection& connection, const ServerRequest& request) {
    constexpr std::string_view kPrefix = "/hop/";
    if (request.target.starts_with(kPrefix)) {
      const int remaining = std::stoi(request.target.substr(kPrefix.size()));
      if (remaining > 0) {
        test::SendResponse(connection, 302,
                           {{.name = "Location",
                             .value = absl::StrCat("/hop/", remaining - 1)}},
                           "");
        return;
      }
    }
    test::SendResponse(connection, 200, {}, *body);
  };
}

TEST_F(CurlTransportTest, FollowsARedirectChainAndReportsTheFinalUrl) {
  StartServer(RedirectChain("arrived"));
  HttpRequest request = Get(server_->BaseUrl() + "/hop/3");
  request.redirects = {.max_redirects = 5, .allow_https_downgrade = false};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->head->status, 200);
  EXPECT_EQ(collector->head->final_url, server_->BaseUrl() + "/hop/0");
  EXPECT_EQ(collector->metrics->final_url, server_->BaseUrl() + "/hop/0");
  EXPECT_EQ(collector->body, "arrived");
  EXPECT_EQ(server_->Requests().size(), 4U);
}

TEST_F(CurlTransportTest, ARedirectChainOverTheLimitFails) {
  StartServer(RedirectChain("arrived"));
  HttpRequest request = Get(server_->BaseUrl() + "/hop/5");
  request.redirects = {.max_redirects = 2, .allow_https_downgrade = false};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->error.has_value());
  EXPECT_EQ(collector->error->code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(collector->head.has_value());
}

TEST_F(CurlTransportTest, ZeroRedirectsMeansTheRedirectIsRefused) {
  StartServer(RedirectChain("arrived"));
  HttpRequest request = Get(server_->BaseUrl() + "/hop/1");
  request.redirects = {.max_redirects = 0, .allow_https_downgrade = false};
  const auto collector = Fetch(std::move(request));
  ASSERT_TRUE(collector->error.has_value());
  EXPECT_EQ(collector->error->code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(CurlTransportTest, AnUntrustedCertificateFailsWithATlsError) {
  StartServer(FileHandler(Pattern(100), false), true);
  const auto collector = Fetch(Get(server_->BaseUrl() + "/media.mp4"));
  ASSERT_TRUE(collector->error.has_value());
  EXPECT_EQ(collector->error->code(), absl::StatusCode::kUnauthenticated);
  EXPECT_NE(std::string(collector->error->message()).find("TLS"),
            std::string::npos);
  EXPECT_FALSE(collector->head.has_value());
  EXPECT_TRUE(server_->Requests().empty());
}

TEST_F(CurlTransportTest, ATrustedCertificateServesOverTls) {
  StartServer(FileHandler(Pattern(2000), false), true);
  const auto transport = TrustingTransport();
  const auto collector =
      Fetch(Get(server_->BaseUrl() + "/media.mp4"), transport.get());
  ASSERT_TRUE(collector->metrics.has_value())
      << collector->error.value_or(absl::OkStatus());
  EXPECT_EQ(collector->head->status, 200);
  EXPECT_EQ(collector->body, Pattern(2000));
  EXPECT_TRUE(collector->metrics->tls.has_value());
}

TEST_F(CurlTransportTest, AnHttpsToHttpRedirectIsRefusedByDefault) {
  auto plain = HttpTestServer::Start(false, FileHandler(Pattern(300), false));
  ASSERT_TRUE(plain.ok());
  const auto target =
      std::make_shared<const std::string>((*plain)->BaseUrl() + "/media.mp4");
  StartServer(
      [target](ServerConnection& connection, const ServerRequest&) {
        test::SendResponse(connection, 302,
                           {{.name = "Location", .value = *target}}, "");
      },
      true);
  const auto transport = TrustingTransport();
  const auto refused = Fetch(Get(server_->BaseUrl() + "/go"), transport.get());
  ASSERT_TRUE(refused->error.has_value());
  EXPECT_EQ(refused->error->code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE((*plain)->Requests().empty());

  HttpRequest allowed = Get(server_->BaseUrl() + "/go");
  allowed.redirects = {.max_redirects = 5, .allow_https_downgrade = true};
  const auto followed = Fetch(std::move(allowed), transport.get());
  ASSERT_TRUE(followed->metrics.has_value());
  EXPECT_EQ(followed->head->status, 200);
  EXPECT_EQ(followed->head->final_url, *target);
  EXPECT_EQ(followed->body, Pattern(300));
}

TEST_F(CurlTransportTest, AnHttpToHttpsRedirectIsFollowed) {
  StartServer(FileHandler(Pattern(300), false), true);
  const auto target =
      std::make_shared<const std::string>(server_->BaseUrl() + "/media.mp4");
  auto plain = HttpTestServer::Start(
      false, [target](ServerConnection& connection, const ServerRequest&) {
        test::SendResponse(connection, 302,
                           {{.name = "Location", .value = *target}}, "");
      });
  ASSERT_TRUE(plain.ok());
  const auto transport = TrustingTransport();
  const auto collector =
      Fetch(Get((*plain)->BaseUrl() + "/go"), transport.get());
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->head->final_url, *target);
}

TEST_F(CurlTransportTest, ARefusedConnectionIsUnavailable) {
  int port = 0;
  {
    auto closed = HttpTestServer::Start(false, FileHandler("", false));
    ASSERT_TRUE(closed.ok());
    port = (*closed)->Port();
  }
  const auto collector =
      Fetch(Get(absl::StrCat("http://localhost:", port, "/x")));
  ASSERT_TRUE(collector->error.has_value());
  EXPECT_EQ(collector->error->code(), absl::StatusCode::kUnavailable);
}

TEST_F(CurlTransportTest, PauseHoldsTheServerBackAndResumeContinues) {
  constexpr size_t kTotal = 48 * kMebibyte;
  const auto sent = std::make_shared<std::atomic<size_t>>(0);
  StartServer([sent, this](ServerConnection& connection, const ServerRequest&) {
    test::SendHead(connection, 200,
                   {{.name = "Content-Length", .value = absl::StrCat(kTotal)}});
    const std::string piece = Pattern(65536);
    for (size_t position = 0;
         position < kTotal && connection.Open() && !server_->Stopping();
         position += piece.size()) {
      if (!connection.Write(piece)) {
        break;
      }
      sent->fetch_add(piece.size());
    }
  });
  auto collector = std::make_shared<Collector>();
  auto handle = transport_->Start(Get(server_->BaseUrl() + "/big"), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(
      collector->WaitFor([&] { return collector->body.size() >= 65536; }));
  ASSERT_TRUE((*handle)->Pause().ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const size_t held = collector->BodySize();
  const size_t sent_when_held = sent->load();
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  EXPECT_EQ(collector->BodySize(), held);
  EXPECT_LT(sent->load(), kTotal);
  EXPECT_LE(sent->load(), sent_when_held + (4 * kMebibyte));
  EXPECT_FALSE(collector->metrics.has_value());
  ASSERT_TRUE((*handle)->Resume().ok());
  ASSERT_TRUE(collector->WaitTerminal());
  ASSERT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->body.size(), kTotal);
  EXPECT_EQ(collector->body.substr(0, 65536), Pattern(65536));
  EXPECT_EQ((*handle)->Pause().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*handle)->Resume().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(CurlTransportTest, CancelStopsCallbacksAndClosesTheConnection) {
  const auto closed = std::make_shared<std::atomic<bool>>(false);
  StartServer(
      [closed, this](ServerConnection& connection, const ServerRequest&) {
        test::SendHead(connection, 200,
                       {{.name = "Content-Length", .value = "100000000"}});
        const std::string piece = Pattern(16384);
        while (connection.Open() && !server_->Stopping()) {
          if (!connection.Write(piece)) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        closed->store(true);
      });
  auto collector = std::make_shared<Collector>();
  auto handle = transport_->Start(Get(server_->BaseUrl() + "/live"), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(collector->WaitFor([&] { return !collector->body.empty(); }));
  (*handle)->Cancel();
  (*handle)->Cancel();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const size_t events = collector->EventCount();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(collector->EventCount(), events);
  EXPECT_FALSE(collector->metrics.has_value());
  EXPECT_FALSE(collector->error.has_value());
  for (int i = 0; i < 200 && !closed->load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(closed->load());
}

TEST_F(CurlTransportTest, DestroyingTheHandleCancelsTheRequest) {
  StartServer([this](ServerConnection& connection, const ServerRequest&) {
    test::SendHead(connection, 200,
                   {{.name = "Content-Length", .value = "10"}});
    while (connection.Open() && !server_->Stopping()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  auto collector = std::make_shared<Collector>();
  {
    auto handle =
        transport_->Start(Get(server_->BaseUrl() + "/slow"), collector);
    ASSERT_TRUE(handle.ok());
    ASSERT_TRUE(Poll([&] { return server_->Requests().size() == 1; }));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(collector->error.has_value());
  EXPECT_FALSE(collector->metrics.has_value());
}

TEST_F(CurlTransportTest, StartValidatesTheRequest) {
  const auto start = [&](HttpRequest request) {
    return transport_->Start(std::move(request), std::make_shared<Collector>())
        .status()
        .code();
  };
  EXPECT_EQ(start(HttpRequest{}), absl::StatusCode::kInvalidArgument);
  HttpRequest timeout = Get("http://localhost:1/");
  timeout.connect_timeout = absl::ZeroDuration();
  EXPECT_EQ(start(timeout), absl::StatusCode::kInvalidArgument);
  HttpRequest redirects = Get("http://localhost:1/");
  redirects.redirects.max_redirects = -1;
  EXPECT_EQ(start(redirects), absl::StatusCode::kInvalidArgument);
  HttpRequest body = Get("http://localhost:1/");
  body.body = SharedBytes::Copy(std::vector<uint8_t>{1});
  EXPECT_EQ(start(body), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      transport_->Start(Get("http://localhost:1/"), nullptr).status().code(),
      absl::StatusCode::kInvalidArgument);
}

TEST_F(CurlTransportTest, StopRejectsLaterRequestsAndSilencesCallbacks) {
  StartServer([this](ServerConnection& connection, const ServerRequest&) {
    test::SendHead(connection, 200,
                   {{.name = "Content-Length", .value = "10"}});
    while (connection.Open() && !server_->Stopping()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  auto collector = std::make_shared<Collector>();
  auto handle = transport_->Start(Get(server_->BaseUrl() + "/slow"), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(Poll([&] { return server_->Requests().size() == 1; }));
  EXPECT_TRUE(transport_->Stop().ok());
  EXPECT_EQ(transport_->Start(Get(server_->BaseUrl() + "/x"), collector)
                .status()
                .code(),
            absl::StatusCode::kFailedPrecondition);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(collector->metrics.has_value());
  EXPECT_FALSE(collector->error.has_value());
}

class RespondingCredentials final : public umbra::source::CredentialProvider {
 public:
  void RequestCredential(const umbra::source::CredentialChallenge& challenge,
                         umbra::source::CredentialResponse respond) override {
    challenges.push_back(challenge);
    std::move(respond)(Credential{.username = "user", .password = "pass"});
  }
  std::vector<umbra::source::CredentialChallenge> challenges;
};

class RecordingObserver final : public umbra::source::HttpRequestObserver {
 public:
  void OnRequestComplete(
      const umbra::source::HttpRequestReport& report) override {
    reports.push_back(report);
  }
  std::vector<umbra::source::HttpRequestReport> reports;
};

class StreamRecorder final : public ByteStreamCallbacks {
 public:
  absl::Status OnOpen(ByteStreamInfo i) override {
    info = std::move(i);
    return absl::OkStatus();
  }
  absl::Status OnData(ByteStreamChunk chunk) override {
    const auto view = chunk.data.span();
    data.append(reinterpret_cast<const char*>(view.data()), view.size());
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
  std::optional<ByteStreamInfo> info;
  std::string data;
  bool ended = false;
  std::optional<absl::Status> error;
};

class LiveSourceTest : public CurlTransportTest {
 protected:
  LiveSourceTest()
      : executor_(umbra::base::GetDefaultTickClock()),
        runner_(executor_.CreateSequencedTaskRunner(
            umbra::base::TaskPriority::kControl)),
        redactor_(std::make_shared<umbra::base::UrlRedactor>()),
        credentials_(std::make_shared<RespondingCredentials>()),
        observer_(std::make_shared<RecordingObserver>()) {}

  void TearDown() override {
    runner_->PostTask([&] {
      sources_.clear();
      streams_.clear();
    });
    executor_.RunUntilIdle();
    if (server_ != nullptr) {
      server_->Stop();
    }
  }

  void MakeProvider(
      umbra::source::HttpByteStreamOptions options = {},
      const std::shared_ptr<CurlHttpTransport>& transport = nullptr) {
    umbra::source::HttpByteStreamDependencies dependencies;
    dependencies.transport = transport != nullptr ? transport : transport_;
    dependencies.runner = runner_;
    dependencies.redactor = redactor_;
    dependencies.credentials = credentials_;
    dependencies.observer = observer_;
    auto provider = umbra::source::HttpByteStreamProvider::Create(
        std::move(dependencies), std::move(options));
    ASSERT_TRUE(provider.ok());
    provider_ = *std::move(provider);
  }

  bool RunUntil(const std::function<bool()>& done) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
      executor_.RunUntilIdle();
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  absl::StatusOr<std::shared_ptr<umbra::source::DataSource>> OpenSource(
      const std::string& url, uint64_t offset = 0) {
    std::optional<absl::StatusOr<std::shared_ptr<umbra::source::DataSource>>>
        result;
    std::shared_ptr<umbra::source::OpenRequest> request;
    runner_->PostTask([&] {
      ByteStreamRequest stream_request;
      stream_request.url = url;
      stream_request.offset = offset;
      request = umbra::source::ByteStreamDataSource::Open(
          provider_, std::move(stream_request), runner_, redactor_, {},
          [&](absl::StatusOr<std::shared_ptr<umbra::source::DataSource>> r) {
            result = std::move(r);
          });
    });
    EXPECT_TRUE(RunUntil([&] { return result.has_value(); }));
    if (!result.has_value()) {
      return absl::InternalError("open did not finish");
    }
    if (result->ok()) {
      sources_.push_back(**result);
    }
    return *std::move(result);
  }

  absl::StatusOr<std::string> ReadAt(umbra::source::DataSource& source,
                                     uint64_t offset, size_t size) {
    std::optional<absl::StatusOr<SharedBytes>> result;
    std::shared_ptr<umbra::source::ReadRequest> request;
    runner_->PostTask([&] {
      request = source.Read(offset, size, [&](absl::StatusOr<SharedBytes> r) {
        result = std::move(r);
      });
    });
    EXPECT_TRUE(RunUntil([&] { return result.has_value(); }));
    if (!result.has_value()) {
      return absl::InternalError("read did not finish");
    }
    if (!result->ok()) {
      return result->status();
    }
    const auto view = (*result)->span();
    return std::string(reinterpret_cast<const char*>(view.data()), view.size());
  }

  static umbra::base::ErrorCode CodeOf(const absl::Status& status) {
    const auto detail = umbra::base::GetErrorDetail(status);
    EXPECT_TRUE(detail.ok() && detail->has_value());
    return detail->value().code;
  }

  umbra::base::SingleThreadTaskExecutor executor_;
  std::shared_ptr<umbra::base::SequencedTaskRunner> runner_;
  std::shared_ptr<umbra::base::UrlRedactor> redactor_;
  std::shared_ptr<RespondingCredentials> credentials_;
  std::shared_ptr<RecordingObserver> observer_;
  std::shared_ptr<umbra::source::HttpByteStreamProvider> provider_;
  std::vector<std::shared_ptr<umbra::source::DataSource>> sources_;
  std::vector<std::unique_ptr<ByteStream>> streams_;
};

TEST_F(LiveSourceTest, SeekingIssuesANewRangeRequestAndNeverRestartsAtZero) {
  const std::string content = Pattern(4 * kMebibyte);
  StartServer(FileHandler(content, true));
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4");
  ASSERT_TRUE(source.ok());
  const umbra::source::DataSourceInfo& info = (*source)->Info();
  EXPECT_TRUE(info.seekable);
  EXPECT_EQ(info.size, content.size());
  EXPECT_EQ(info.content_type, "video/mp4");
  EXPECT_EQ(info.final_url, server_->BaseUrl() + "/movie.mp4");

  const auto head = ReadAt(**source, 0, 5000);
  ASSERT_TRUE(head.ok());
  EXPECT_EQ(*head, content.substr(0, 5000));
  const uint64_t target = (3 * kMebibyte) + 123;
  const auto sought = ReadAt(**source, target, 70000);
  ASSERT_TRUE(sought.ok());
  EXPECT_EQ(*sought, content.substr(target, 70000));
  const auto back = ReadAt(**source, 1000000, 1000);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(*back, content.substr(1000000, 1000));

  const auto requests = server_->Requests();
  size_t from_zero = 0;
  bool saw_target = false;
  bool saw_back = false;
  for (const ServerRequest& request : requests) {
    const std::string_view range = Header(request, "range");
    from_zero += range == "bytes=0-" ? 1 : 0;
    saw_target = saw_target || range == absl::StrCat("bytes=", target, "-");
    saw_back = saw_back || range == "bytes=1000000-";
  }
  EXPECT_EQ(from_zero, 1U);
  EXPECT_TRUE(saw_target);
  EXPECT_TRUE(saw_back);
}

TEST_F(LiveSourceTest, AServerThatIgnoresRangesIsSkippedToTheOffset) {
  const std::string content = Pattern(600000);
  StartServer(FileHandler(content, false));
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4");
  ASSERT_TRUE(source.ok());
  EXPECT_FALSE((*source)->Info().seekable);
  EXPECT_EQ((*source)->Info().size, content.size());
  const auto head = ReadAt(**source, 0, 1000);
  ASSERT_TRUE(head.ok());
  EXPECT_EQ(*head, content.substr(0, 1000));
  const auto next = ReadAt(**source, 1000, 1000);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(*next, content.substr(1000, 1000));
  const auto seek = ReadAt(**source, 300000, 10);
  EXPECT_EQ(seek.status().code(), absl::StatusCode::kOutOfRange);

  auto recorder = std::make_shared<StreamRecorder>();
  runner_->PostTask([&] {
    ByteStreamRequest request;
    request.url = server_->BaseUrl() + "/movie.mp4";
    request.offset = 400000;
    auto stream = provider_->Open(std::move(request), recorder);
    ASSERT_TRUE(stream.ok());
    streams_.push_back(*std::move(stream));
  });
  ASSERT_TRUE(RunUntil([&] { return recorder->ended; }));
  EXPECT_EQ(recorder->data, content.substr(400000));
}

TEST_F(LiveSourceTest, AnOffsetAtTheEndIsAnEmptyStreamFrom416) {
  const std::string content = Pattern(1234);
  StartServer(FileHandler(content, true));
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4", content.size());
  ASSERT_TRUE(source.ok());
  EXPECT_EQ((*source)->Info().size, content.size());
  const auto empty = ReadAt(**source, content.size(), 10);
  ASSERT_TRUE(empty.ok());
  EXPECT_TRUE(empty->empty());
}

TEST_F(LiveSourceTest, ABasicChallengeIsAnsweredThroughTheCredentialProvider) {
  const auto content = std::make_shared<const std::string>(Pattern(2000));
  StartServer([content](ServerConnection& connection,
                        const ServerRequest& request) {
    if (Header(request, "authorization") == "Basic dXNlcjpwYXNz") {
      test::SendResponse(
          connection, 206,
          {{.name = "Content-Range",
            .value = absl::StrCat("bytes 0-", content->size() - 1, "/",
                                  content->size())}},
          *content);
    } else {
      test::SendResponse(
          connection, 401,
          {{.name = "WWW-Authenticate", .value = R"(Basic realm="Cameras")"}},
          "");
    }
  });
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/stream.mp4");
  ASSERT_TRUE(source.ok());
  ASSERT_EQ(credentials_->challenges.size(), 1U);
  EXPECT_EQ(credentials_->challenges[0].origin, server_->BaseUrl());
  EXPECT_EQ(credentials_->challenges[0].realm, "Cameras");
  const auto bytes = ReadAt(**source, 0, 2000);
  ASSERT_TRUE(bytes.ok());
  EXPECT_EQ(*bytes, *content);
  const auto requests = server_->Requests();
  ASSERT_EQ(requests.size(), 2U);
  EXPECT_TRUE(Header(requests[0], "authorization").empty());
  EXPECT_EQ(Header(requests[1], "authorization"), "Basic dXNlcjpwYXNz");
}

TEST_F(LiveSourceTest, ASilentServerTripsTheIdleReadTimer) {
  StartServer([this](ServerConnection& connection, const ServerRequest&) {
    while (connection.Open() && !server_->Stopping()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  MakeProvider({.connect_timeout = absl::Milliseconds(200),
                .read_timeout = absl::Milliseconds(300),
                .max_redirects = 20,
                .allow_https_to_http_redirect = false,
                .user_agent = "",
                .headers = {}});
  const auto start = std::chrono::steady_clock::now();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4");
  ASSERT_FALSE(source.ok());
  EXPECT_EQ(CodeOf(source.status()), umbra::base::ErrorCode::kNetworkTimeout);
  EXPECT_GE(std::chrono::steady_clock::now() - start,
            std::chrono::milliseconds(450));
}

TEST_F(LiveSourceTest,
       ASelfSignedCertificateFailsWithATlsErrorThatIsNotRetryable) {
  StartServer(FileHandler(Pattern(100), true), true);
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4");
  ASSERT_FALSE(source.ok());
  EXPECT_EQ(CodeOf(source.status()), umbra::base::ErrorCode::kNetworkTlsFailed);
  const auto detail = umbra::base::GetErrorDetail(source.status());
  ASSERT_TRUE(detail.ok() && detail->has_value());
  EXPECT_FALSE(detail->value().retryable);
  EXPECT_TRUE(server_->Requests().empty());
}

TEST_F(LiveSourceTest, RedirectLimitsAndDowngradesAreRefused) {
  StartServer(RedirectChain("x"));
  MakeProvider({.connect_timeout = absl::Seconds(10),
                .read_timeout = absl::Seconds(20),
                .max_redirects = 1,
                .allow_https_to_http_redirect = false,
                .user_agent = "",
                .headers = {}});
  auto source = OpenSource(server_->BaseUrl() + "/hop/3");
  ASSERT_FALSE(source.ok());
  EXPECT_EQ(CodeOf(source.status()),
            umbra::base::ErrorCode::kNetworkRedirectRefused);
}

TEST_F(LiveSourceTest, ServerErrorsKeepTheirStatusAndRetryAfter) {
  StartServer([](ServerConnection& connection, const ServerRequest&) {
    test::SendResponse(connection, 503, {{.name = "Retry-After", .value = "2"}},
                       "busy");
  });
  MakeProvider();
  auto source = OpenSource(server_->BaseUrl() + "/movie.mp4");
  ASSERT_FALSE(source.ok());
  EXPECT_EQ(CodeOf(source.status()),
            umbra::base::ErrorCode::kNetworkServerError);
  const auto detail = umbra::base::GetErrorDetail(source.status());
  ASSERT_TRUE(detail.ok() && detail->has_value());
  EXPECT_EQ(detail->value().context.at("retry_after"), "2");
  EXPECT_EQ(server_->Requests().size(), 1U);
}

TEST_F(LiveSourceTest, EveryRequestReportsItsTiming) {
  const std::string content = Pattern(200000);
  StartServer(FileHandler(content, true));
  MakeProvider();
  auto recorder = std::make_shared<StreamRecorder>();
  runner_->PostTask([&] {
    ByteStreamRequest request;
    request.url = server_->BaseUrl() + "/movie.mp4";
    auto stream = provider_->Open(std::move(request), recorder);
    ASSERT_TRUE(stream.ok());
    streams_.push_back(*std::move(stream));
  });
  ASSERT_TRUE(RunUntil([&] { return recorder->ended; }));
  EXPECT_EQ(recorder->data, content);
  ASSERT_EQ(observer_->reports.size(), 1U);
  const umbra::source::HttpRequestReport& report = observer_->reports[0];
  EXPECT_EQ(report.status, 206);
  EXPECT_EQ(report.body_bytes, content.size());
  EXPECT_EQ(report.protocol, HttpProtocol::kHttp1_1);
  EXPECT_GE(report.time_to_first_byte, absl::ZeroDuration());
  EXPECT_GE(report.transfer_duration, absl::ZeroDuration());
  EXPECT_EQ(report.paused_duration, absl::ZeroDuration());
  EXPECT_FALSE(report.coarse_timing);
}

}
}
