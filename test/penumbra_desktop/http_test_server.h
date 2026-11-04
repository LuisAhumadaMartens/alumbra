#ifndef TEST_PENUMBRA_DESKTOP_HTTP_TEST_SERVER_H_
#define TEST_PENUMBRA_DESKTOP_HTTP_TEST_SERVER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace penumbra::desktop::test {

struct ServerRequest {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;
  std::string body;
};

class ServerConnection {
 public:
  virtual ~ServerConnection() = default;
  virtual bool Write(std::string_view bytes) = 0;
  [[nodiscard]] virtual bool Open() const = 0;
};

using ServerHandler =
    std::function<void(ServerConnection&, const ServerRequest&)>;

struct ServerHeader {
  std::string name;
  std::string value;
};

bool SendResponse(ServerConnection& connection, int status,
                  const std::vector<ServerHeader>& headers,
                  std::string_view body);

bool SendHead(ServerConnection& connection, int status,
              const std::vector<ServerHeader>& headers);

class HttpTestServer {
 public:
  static absl::StatusOr<std::unique_ptr<HttpTestServer>> Start(
      bool tls, ServerHandler handler);

  HttpTestServer(const HttpTestServer&) = delete;
  HttpTestServer& operator=(const HttpTestServer&) = delete;
  ~HttpTestServer();

  void Stop();

  [[nodiscard]] int Port() const { return port_; }
  [[nodiscard]] std::string BaseUrl() const;
  [[nodiscard]] std::vector<ServerRequest> Requests() const;
  [[nodiscard]] const std::string& CertificatePem() const { return pem_; }
  [[nodiscard]] bool Stopping() const { return stopping_.load(); }

 private:
  struct Tls;

  HttpTestServer(bool tls, ServerHandler handler);

  void AcceptLoop();
  void Serve(int fd);

  bool tls_;
  ServerHandler handler_;
  int listen_fd_ = -1;
  int port_ = 0;
  std::string pem_;
  std::unique_ptr<Tls> tls_state_;
  std::atomic<bool> stopping_{false};
  std::thread acceptor_;
  mutable std::mutex mutex_;
  std::vector<std::thread> workers_;
  std::vector<int> open_fds_;
  std::vector<ServerRequest> requests_;
};

}

#endif
