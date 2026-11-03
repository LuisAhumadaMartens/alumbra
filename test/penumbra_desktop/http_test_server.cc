#include "http_test_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace penumbra::desktop::test {

using SslHandle = decltype(SSL_new(nullptr));
using SslContextHandle = decltype(SSL_CTX_new(nullptr));
using KeyHandle = decltype(EVP_PKEY_new());
using CertificateHandle = decltype(X509_new());

struct HttpTestServer::Tls {
  SslContextHandle context = nullptr;
  KeyHandle key = nullptr;
  CertificateHandle certificate = nullptr;

  Tls() = default;
  Tls(const Tls&) = delete;
  Tls& operator=(const Tls&) = delete;
  ~Tls() {
    SSL_CTX_free(context);
    EVP_PKEY_free(key);
    X509_free(certificate);
  }
};

namespace {

constexpr int kSigPipe = 13;

int HostPort(uint16_t network_order) {
  if constexpr (std::endian::native == std::endian::little) {
    return static_cast<uint16_t>((network_order >> 8) | (network_order << 8));
  }
  return network_order;
}

const char* ReasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 206:
      return "Partial Content";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 401:
      return "Unauthorized";
    case 404:
      return "Not Found";
    case 416:
      return "Range Not Satisfiable";
    case 503:
      return "Service Unavailable";
    default:
      return "Status";
  }
}

class PlainConnection final : public ServerConnection {
 public:
  explicit PlainConnection(int fd) : fd_(fd) {}

  bool Write(std::string_view bytes) override {
    while (!bytes.empty() && open_) {
      const std::ptrdiff_t written = ::send(fd_, bytes.data(), bytes.size(), 0);
      if (written <= 0) {
        open_ = false;
        return false;
      }
      bytes.remove_prefix(static_cast<size_t>(written));
    }
    return open_;
  }

  [[nodiscard]] bool Open() const override { return open_; }

 private:
  int fd_;
  bool open_ = true;
};

class TlsConnection final : public ServerConnection {
 public:
  explicit TlsConnection(SslHandle ssl) : ssl_(ssl) {}

  bool Write(std::string_view bytes) override {
    while (!bytes.empty() && open_) {
      const int written =
          SSL_write(ssl_, bytes.data(), static_cast<int>(bytes.size()));
      if (written <= 0) {
        open_ = false;
        return false;
      }
      bytes.remove_prefix(static_cast<size_t>(written));
    }
    return open_;
  }

  [[nodiscard]] bool Open() const override { return open_; }

 private:
  SslHandle ssl_;
  bool open_ = true;
};

std::string Lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return text;
}

bool ParseRequest(const std::string& raw, ServerRequest& request) {
  const size_t line_end = raw.find("\r\n");
  if (line_end == std::string::npos) {
    return false;
  }
  const std::string line = raw.substr(0, line_end);
  const size_t first = line.find(' ');
  const size_t second = line.find(' ', first + 1);
  if (first == std::string::npos || second == std::string::npos) {
    return false;
  }
  request.method = line.substr(0, first);
  request.target = line.substr(first + 1, second - first - 1);
  size_t position = line_end + 2;
  while (position < raw.size()) {
    const size_t end = raw.find("\r\n", position);
    if (end == std::string::npos || end == position) {
      break;
    }
    const std::string header = raw.substr(position, end - position);
    const size_t colon = header.find(':');
    if (colon != std::string::npos) {
      size_t start = colon + 1;
      while (start < header.size() && header[start] == ' ') {
        ++start;
      }
      request.headers[Lower(header.substr(0, colon))] = header.substr(start);
    }
    position = end + 2;
  }
  return true;
}

}

bool SendHead(ServerConnection& connection, int status,
              const std::vector<ServerHeader>& headers) {
  std::string head =
      absl::StrCat("HTTP/1.1 ", status, " ", ReasonPhrase(status), "\r\n");
  for (const ServerHeader& header : headers) {
    absl::StrAppend(&head, header.name, ": ", header.value, "\r\n");
  }
  absl::StrAppend(&head, "Connection: close\r\n\r\n");
  return connection.Write(head);
}

bool SendResponse(ServerConnection& connection, int status,
                  const std::vector<ServerHeader>& headers,
                  std::string_view body) {
  std::vector<ServerHeader> all = headers;
  all.push_back({.name = "Content-Length", .value = absl::StrCat(body.size())});
  return SendHead(connection, status, all) && connection.Write(body);
}

absl::StatusOr<std::unique_ptr<HttpTestServer>> HttpTestServer::Start(
    bool tls, ServerHandler handler) {
  static_cast<void>(std::signal(kSigPipe, SIG_IGN));
  std::unique_ptr<HttpTestServer> server(
      new HttpTestServer(tls, std::move(handler)));
  if (tls) {
    auto state = std::make_unique<Tls>();
    state->key = EVP_EC_gen("P-256");
    state->certificate = X509_new();
    if (state->key == nullptr || state->certificate == nullptr) {
      return absl::InternalError("cannot create the test key");
    }
    const CertificateHandle certificate = state->certificate;
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
    X509_gmtime_adj(X509_getm_notAfter(certificate), 86400);
    X509_set_pubkey(certificate, state->key);
    auto* name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(certificate, name);
    if (X509_sign(certificate, state->key, EVP_sha256()) == 0) {
      return absl::InternalError("cannot sign the test certificate");
    }
    auto* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, certificate);
    const char* data = nullptr;
    const auto size = BIO_get_mem_data(bio, &data);
    server->pem_.assign(data, static_cast<size_t>(size));
    BIO_free(bio);
    state->context = SSL_CTX_new(TLS_server_method());
    if (state->context == nullptr ||
        SSL_CTX_use_certificate(state->context, certificate) != 1 ||
        SSL_CTX_use_PrivateKey(state->context, state->key) != 1) {
      return absl::InternalError("cannot configure the test TLS context");
    }
    server->tls_state_ = std::move(state);
  }
  server->listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server->listen_fd_ < 0) {
    return absl::InternalError("cannot create a socket");
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  address.sin_port = 0;
  if (::bind(server->listen_fd_, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
      ::listen(server->listen_fd_, 64) != 0) {
    return absl::InternalError("cannot listen on the loopback interface");
  }
  unsigned int length = sizeof(address);
  getsockname(server->listen_fd_, reinterpret_cast<sockaddr*>(&address),
              &length);
  server->port_ = HostPort(address.sin_port);
  server->acceptor_ = std::thread([raw = server.get()] { raw->AcceptLoop(); });
  return server;
}

HttpTestServer::HttpTestServer(bool tls, ServerHandler handler)
    : tls_(tls), handler_(std::move(handler)) {}

HttpTestServer::~HttpTestServer() { Stop(); }

std::string HttpTestServer::BaseUrl() const {
  return absl::StrCat(tls_ ? "https" : "http", "://localhost:", port_);
}

std::vector<ServerRequest> HttpTestServer::Requests() const {
  const std::scoped_lock lock(mutex_);
  return requests_;
}

void HttpTestServer::Stop() {
  if (stopping_.exchange(true)) {
    return;
  }
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
  }
  if (acceptor_.joinable()) {
    acceptor_.join();
  }
  std::vector<std::thread> workers;
  {
    const std::scoped_lock lock(mutex_);
    for (const int fd : open_fds_) {
      ::shutdown(fd, SHUT_RDWR);
    }
    workers.swap(workers_);
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
}

void HttpTestServer::AcceptLoop() {
  while (!stopping_) {
    pollfd descriptor{.fd = listen_fd_, .events = POLLIN, .revents = 0};
    if (::poll(&descriptor, 1, 50) <= 0) {
      continue;
    }
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      continue;
    }
    const std::scoped_lock lock(mutex_);
    open_fds_.push_back(fd);
    workers_.emplace_back([this, fd] { Serve(fd); });
  }
}

void HttpTestServer::Serve(int fd) {
  SslHandle ssl = nullptr;
  std::unique_ptr<ServerConnection> connection;
  auto read_some = [&](char* buffer, size_t size) -> std::ptrdiff_t {
    if (ssl != nullptr) {
      const int got = SSL_read(ssl, buffer, static_cast<int>(size));
      return got;
    }
    return ::recv(fd, buffer, size, 0);
  };
  if (tls_) {
    ssl = SSL_new(tls_state_->context);
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) != 1) {
      SSL_free(ssl);
      ::close(fd);
      return;
    }
    connection = std::make_unique<TlsConnection>(ssl);
  } else {
    connection = std::make_unique<PlainConnection>(fd);
  }
  std::string raw;
  std::array<char, 4096> buffer{};
  while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
    const std::ptrdiff_t got = read_some(buffer.data(), buffer.size());
    if (got <= 0) {
      break;
    }
    raw.append(buffer.data(), static_cast<size_t>(got));
  }
  ServerRequest request;
  if (raw.find("\r\n\r\n") != std::string::npos && ParseRequest(raw, request)) {
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
      const size_t expected = std::stoul(length->second);
      request.body = raw.substr(raw.find("\r\n\r\n") + 4);
      while (request.body.size() < expected) {
        const std::ptrdiff_t got = read_some(buffer.data(), buffer.size());
        if (got <= 0) {
          break;
        }
        request.body.append(buffer.data(), static_cast<size_t>(got));
      }
    }
    {
      const std::scoped_lock lock(mutex_);
      requests_.push_back(request);
    }
    handler_(*connection, request);
  }
  if (ssl != nullptr) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
  }
  {
    const std::scoped_lock lock(mutex_);
    std::erase(open_fds_, fd);
  }
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
}

}
