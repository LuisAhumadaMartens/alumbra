#include "penumbra/apple/url_session_http_transport.h"

#import <Foundation/Foundation.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/apple/url_session_metrics.h"
#include "penumbra/interface/http_transport.h"

namespace penumbra::apple {
namespace {

class LocalServer {
 public:
  LocalServer() : socket_(::socket(AF_INET, SOCK_STREAM, 0)) {
    EXPECT_GE(socket_, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    EXPECT_EQ(::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    socklen_t length = sizeof(address);
    EXPECT_EQ(::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length), 0);
    port_ = ntohs(address.sin_port);
    EXPECT_EQ(::listen(socket_, 1), 0);
    const int no_sigpipe = 1;
    EXPECT_EQ(::setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)), 0);
    worker_ = std::thread([this] { Serve(); });
  }

  LocalServer(const LocalServer&) = delete;
  LocalServer& operator=(const LocalServer&) = delete;
  ~LocalServer() { Stop(); }

  void AllowCompletion() {
    const std::scoped_lock lock(mutex_);
    finish_ = true;
    condition_.notify_all();
  }

  void Stop() {
    if (stopped_.exchange(true)) {
      return;
    }
    {
      const std::scoped_lock lock(mutex_);
      finish_ = true;
      condition_.notify_all();
    }
    if (socket_ >= 0) {
      ::shutdown(socket_, SHUT_RDWR);
      ::close(socket_);
      socket_ = -1;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::string Url(std::string_view path = "/media") const {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
  }

  std::string Request() const {
    const std::scoped_lock lock(mutex_);
    return request_;
  }

 private:
  void Serve() {
    const int client = ::accept(socket_, nullptr, nullptr);
    if (client < 0) {
      return;
    }
    const int no_delay = 1;
    static_cast<void>(::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)));
    std::string request;
    std::array<char, 1024> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t count = ::recv(client, buffer.data(), buffer.size(), 0);
      if (count <= 0) {
        ::close(client);
        return;
      }
      request.append(buffer.data(), static_cast<size_t>(count));
    }
    {
      const std::scoped_lock lock(mutex_);
      request_ = request;
      condition_.notify_all();
    }
    size_t first = 0;
    size_t last = 7;
    const size_t range = request.find("Range: bytes=");
    const bool trickle = request.find("/trickle ") != std::string::npos;
    const bool timeout = request.find("/timeout ") != std::string::npos;
    if (range != std::string::npos) {
      first = static_cast<size_t>(std::stoull(request.substr(range + 13)));
      last = std::min<size_t>(7, first + 2);
    }
    if (trickle) {
      last = 131071;
    }
    const size_t length = last - first + 1;
    std::string head =
        range == std::string::npos ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 206 Partial Content\r\n";
    head += "Content-Length: " + std::to_string(length) + "\r\nConnection: close\r\n\r\n";
    Send(client, head);
    const std::string body =
        trickle ? std::string(length, 'x') : std::string("abcdefgh").substr(first, length);
    const size_t split = std::min<size_t>(65536, body.size());
    if (!timeout) {
      Send(client, body.substr(0, split));
    }
    if (body.size() > split || timeout) {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, [this] { return finish_ || stopped_.load(); });
      lock.unlock();
      if (body.size() > split) {
        Send(client, body.substr(split));
      }
    }
    ::shutdown(client, SHUT_RDWR);
    ::close(client);
  }

  static void Send(int socket, std::string_view bytes) {
    while (!bytes.empty()) {
      const ssize_t count = ::send(socket, bytes.data(), bytes.size(), 0);
      if (count <= 0) {
        return;
      }
      bytes.remove_prefix(static_cast<size_t>(count));
    }
  }

  int socket_ = -1;
  int port_ = 0;
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::string request_;
  bool finish_ = false;
  std::atomic<bool> stopped_ = false;
};

class Collector final : public HttpTransportCallbacks {
 public:
  absl::Status OnResponse(HttpResponseHead value) override {
    const std::scoped_lock lock(mutex_);
    response = std::move(value);
    condition_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnChunk(HttpChunk value) override {
    const std::scoped_lock lock(mutex_);
    for (const uint8_t byte : value.data.span()) {
      body.push_back(static_cast<char>(byte));
    }
    arrivals.push_back(value.arrival_time);
    chunks.push_back(value.data);
    condition_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnComplete(HttpMetrics value) override {
    const std::scoped_lock lock(mutex_);
    metrics = std::move(value);
    condition_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status value) override {
    const std::scoped_lock lock(mutex_);
    error = std::move(value);
    condition_.notify_all();
    return absl::OkStatus();
  }

  bool WaitForChunk() {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(10),
                               [this] { return !body.empty() || error.has_value(); });
  }
  bool WaitTerminal() {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(10),
                               [this] { return metrics.has_value() || error.has_value(); });
  }

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::optional<HttpResponseHead> response;
  std::string body;
  std::vector<absl::Duration> arrivals;
  std::vector<SharedBytes> chunks;
  std::optional<HttpMetrics> metrics;
  std::optional<absl::Status> error;
};

TEST(UrlSessionHttpTransportTest, StreamsBeforeTheResponseCompletes) {
  LocalServer server;
  auto transport = UrlSessionHttpTransport::Create();
  ASSERT_TRUE(transport.ok());
  auto collector = std::make_shared<Collector>();
  HttpRequest request;
  request.url = server.Url("/trickle");
  auto handle = (*transport)->Start(std::move(request), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(collector->WaitForChunk());
  {
    const std::scoped_lock lock(collector->mutex_);
    EXPECT_FALSE(collector->metrics.has_value());
    EXPECT_GT(collector->body.size(), 0);
    EXPECT_LT(collector->body.size(), 131072);
  }
  server.AllowCompletion();
  ASSERT_TRUE(collector->WaitTerminal());
  EXPECT_TRUE(collector->metrics.has_value());
  EXPECT_EQ(collector->body.size(), 131072);
  EXPECT_EQ(collector->response->status, 200);
  EXPECT_EQ(collector->metrics->body_bytes, 131072);
  EXPECT_LE(collector->metrics->request_start, collector->metrics->response_start);
  EXPECT_LE(collector->metrics->response_start, collector->metrics->completion);
  EXPECT_FALSE(collector->chunks.empty());
  EXPECT_GT(collector->chunks.front().size(), 0);
  EXPECT_EQ((*transport)->Stop(), absl::OkStatus());
}

TEST(UrlSessionHttpTransportTest, SendsRangeAndReportsPartialResponse) {
  LocalServer server;
  auto transport = UrlSessionHttpTransport::Create();
  ASSERT_TRUE(transport.ok());
  auto collector = std::make_shared<Collector>();
  HttpRequest request;
  request.url = server.Url();
  request.range = ByteRange{.offset = 2, .length = 3};
  auto handle = (*transport)->Start(std::move(request), collector);
  ASSERT_TRUE(handle.ok());
  server.AllowCompletion();
  ASSERT_TRUE(collector->WaitTerminal());
  EXPECT_TRUE(server.Request().find("Range: bytes=2-4") != std::string::npos);
  EXPECT_EQ(collector->response->status, 206);
  EXPECT_EQ(collector->body, "cde");
  EXPECT_EQ((*transport)->Stop(), absl::OkStatus());
}

TEST(UrlSessionHttpTransportTest, CancellationSuppressesLaterChunks) {
  LocalServer server;
  auto transport = UrlSessionHttpTransport::Create();
  ASSERT_TRUE(transport.ok());
  auto collector = std::make_shared<Collector>();
  HttpRequest request;
  request.url = server.Url("/trickle");
  auto handle = (*transport)->Start(std::move(request), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(collector->WaitForChunk());
  (*handle)->Cancel();
  std::string body;
  {
    const std::scoped_lock lock(collector->mutex_);
    body = collector->body;
  }
  server.AllowCompletion();
  server.Stop();
  {
    const std::scoped_lock lock(collector->mutex_);
    EXPECT_EQ(collector->body, body);
    EXPECT_FALSE(collector->metrics.has_value());
  }
  EXPECT_EQ((*transport)->Stop(), absl::OkStatus());
}

TEST(UrlSessionHttpTransportTest, RequestTimeoutMapsToDeadlineExceeded) {
  LocalServer server;
  auto transport = UrlSessionHttpTransport::Create();
  ASSERT_TRUE(transport.ok());
  auto collector = std::make_shared<Collector>();
  HttpRequest request;
  request.url = server.Url("/timeout");
  request.connect_timeout = absl::Seconds(1);
  auto handle = (*transport)->Start(std::move(request), collector);
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(collector->WaitTerminal());
  ASSERT_TRUE(collector->error.has_value());
  EXPECT_EQ(collector->error->code(), absl::StatusCode::kDeadlineExceeded);
  server.AllowCompletion();
  server.Stop();
  EXPECT_EQ((*transport)->Stop(), absl::OkStatus());
}

TEST(UrlSessionMetricsTest, MapsNegotiatedProtocolNames) {
  EXPECT_EQ(ProtocolFromName("http/1.1"), HttpProtocol::kHttp1_1);
  EXPECT_EQ(ProtocolFromName("h2"), HttpProtocol::kHttp2);
  EXPECT_EQ(ProtocolFromName("h3"), HttpProtocol::kHttp3);
  EXPECT_EQ(ProtocolFromName("unknown"), HttpProtocol::kUnknown);
}

TEST(UrlSessionMetricsTest, MapsOnlyValidPhaseDurations) {
  EXPECT_EQ(MapSessionPhase(1.25, 1.5), absl::Milliseconds(250));
  EXPECT_FALSE(MapSessionPhase(std::nullopt, 1.5).has_value());
  EXPECT_FALSE(MapSessionPhase(1.5, 1.25).has_value());
}

}
}
