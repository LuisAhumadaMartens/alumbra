#include "penumbra/desktop/curl_http_transport.h"

#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <curl/system.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"

namespace penumbra::desktop {
namespace {

#ifdef __APPLE__
constexpr bool kSystemTrustStore = true;
#else
constexpr bool kSystemTrustStore = false;
#endif

constexpr int kPollMilliseconds = 1000;
constexpr std::string_view kStatusLinePrefix = "HTTP/";

absl::Duration Now() {
  return absl::FromChrono(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()));
}

void InitialiseCurl() {
  static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
  static_cast<void>(result);
}

std::optional<std::string> FindSystemCaBundle() {
  if (kSystemTrustStore) {
    return std::nullopt;
  }
  static constexpr std::array<std::string_view, 6> kCandidates = {
      "/etc/ssl/certs/ca-certificates.crt",
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/ssl/ca-bundle.pem",
      "/etc/pki/tls/cacert.pem",
      "/etc/ssl/cert.pem",
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
  };
  for (const std::string_view candidate : kCandidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(std::string(candidate), ec)) {
      return std::string(candidate);
    }
  }
  return std::nullopt;
}

bool IsSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string Trim(std::string_view text) {
  while (!text.empty() && IsSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && IsSpace(text.back())) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

HttpProtocol ProtocolFrom(std::string_view version) {
  if (version == "1.0") {
    return HttpProtocol::kHttp1_0;
  }
  if (version == "1.1") {
    return HttpProtocol::kHttp1_1;
  }
  if (version == "2" || version == "2.0") {
    return HttpProtocol::kHttp2;
  }
  if (version == "3" || version == "3.0") {
    return HttpProtocol::kHttp3;
  }
  return HttpProtocol::kUnknown;
}

int StatusCodeFrom(std::string_view digits) {
  if (digits.size() != 3) {
    return 0;
  }
  int status = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return 0;
    }
    status = (status * 10) + (c - '0');
  }
  return status;
}

absl::Status StatusFromCurl(CURLcode code, std::string_view detail,
                            bool after_redirect) {
  const std::string message = absl::StrCat(
      curl_easy_strerror(code),
      detail.empty() ? std::string() : absl::StrCat(" (", detail, ")"));
  switch (code) {
    case CURLE_OPERATION_TIMEDOUT:
      return absl::DeadlineExceededError(message);
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_CRL_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
      return absl::UnauthenticatedError(absl::StrCat("TLS: ", message));
    case CURLE_TOO_MANY_REDIRECTS:
      return absl::FailedPreconditionError(
          absl::StrCat("too many redirects: ", message));
    case CURLE_UNSUPPORTED_PROTOCOL:
      if (after_redirect) {
        return absl::FailedPreconditionError(
            absl::StrCat("redirect refused: ", message));
      }
      return absl::InvalidArgumentError(message);
    case CURLE_URL_MALFORMAT:
    case CURLE_BAD_FUNCTION_ARGUMENT:
      return absl::InvalidArgumentError(message);
    case CURLE_ABORTED_BY_CALLBACK:
    case CURLE_WRITE_ERROR:
      return absl::AbortedError(message);
    default:
      return absl::UnavailableError(message);
  }
}

std::optional<absl::Duration> MicrosecondsInfo(CURL* easy, CURLINFO info) {
  curl_off_t value = 0;
  if (curl_easy_getinfo(easy, info, &value) != CURLE_OK || value <= 0) {
    return std::nullopt;
  }
  return absl::Microseconds(value);
}

std::string EffectiveUrl(CURL* easy, const std::string& fallback) {
  const char* url = nullptr;
  curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &url);
  return url != nullptr ? std::string(url) : fallback;
}

bool IsHttps(std::string_view url) {
  constexpr std::string_view kPrefix = "https:";
  return url.size() >= kPrefix.size() &&
         std::ranges::equal(
             url.substr(0, kPrefix.size()), kPrefix, [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == b;
             });
}

std::string Lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return text;
}

}

struct TransferControl {
  std::atomic<bool> cancelled{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> finished{false};
};

class CurlHttpTransport::Core {
 public:
  explicit Core(CurlTransportOptions options)
      : options_(std::move(options)),
        ca_bundle_(options_.ca_bundle_path.has_value() ? options_.ca_bundle_path
                                                       : FindSystemCaBundle()),
        multi_(curl_multi_init()),
        worker_([this] { Run(); }) {}

  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  ~Core() {
    {
      const std::scoped_lock lock(mutex_);
      stopping_ = true;
    }
    Wake();
    worker_.join();
    for (const std::shared_ptr<Transfer>& transfer : active_) {
      Release(*transfer);
    }
    curl_multi_cleanup(multi_);
  }

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Submit(
      HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks,
      const std::shared_ptr<Core>& self) {
    if (callbacks == nullptr || request.url.empty() ||
        request.connect_timeout <= absl::ZeroDuration() ||
        request.redirects.max_redirects < 0 ||
        (request.range.has_value() && request.range->length == 0) ||
        (request.method != HttpMethod::kPost && !request.body.empty())) {
      return absl::InvalidArgumentError("invalid HTTP request");
    }
    auto transfer = std::make_shared<Transfer>();
    transfer->control = std::make_shared<TransferControl>();
    transfer->request = std::move(request);
    transfer->callbacks = std::move(callbacks);
    transfer->request_start = Now();
    {
      const std::scoped_lock lock(mutex_);
      if (stopped_) {
        return absl::FailedPreconditionError("transport is stopped");
      }
      pending_.push_back(transfer);
    }
    Wake();
    return std::make_unique<Handle>(self, transfer->control);
  }

  absl::Status Stop() {
    {
      const std::scoped_lock lock(mutex_);
      stopped_ = true;
      for (const std::shared_ptr<Transfer>& transfer : pending_) {
        transfer->control->cancelled = true;
      }
      for (const std::shared_ptr<TransferControl>& control : controls_) {
        control->cancelled = true;
      }
      if (callbacks_in_flight_ > 0) {
        Wake();
        return absl::UnavailableError(
            "a transport callback is still in flight");
      }
    }
    Wake();
    return absl::OkStatus();
  }

  void Wake() { curl_multi_wakeup(multi_); }

 private:
  struct Transfer {
    HttpRequest request;
    std::shared_ptr<HttpTransportCallbacks> callbacks;
    std::shared_ptr<TransferControl> control;
    Core* core = nullptr;
    CURL* easy = nullptr;
    curl_slist* headers = nullptr;
    std::array<char, CURL_ERROR_SIZE> error{};
    std::vector<HttpHeader> response_headers;
    int status = 0;
    HttpProtocol protocol = HttpProtocol::kUnknown;
    absl::Duration request_start;
    std::optional<absl::Duration> first_arrival;
    uint64_t body_bytes = 0;
    bool head_sent = false;
    bool paused_applied = false;
    bool delivery_failed = false;
  };

  class Handle final : public HttpRequestHandle {
   public:
    Handle(std::shared_ptr<Core> core, std::shared_ptr<TransferControl> control)
        : core_(std::move(core)), control_(std::move(control)) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() override { Cancel(); }

    void Cancel() override {
      control_->cancelled = true;
      core_->Wake();
    }

    absl::Status Pause() override {
      if (control_->cancelled || control_->finished) {
        return absl::FailedPreconditionError("the request is not active");
      }
      control_->paused = true;
      core_->Wake();
      return absl::OkStatus();
    }

    absl::Status Resume() override {
      if (control_->cancelled || control_->finished) {
        return absl::FailedPreconditionError("the request is not active");
      }
      control_->paused = false;
      core_->Wake();
      return absl::OkStatus();
    }

   private:
    std::shared_ptr<Core> core_;
    std::shared_ptr<TransferControl> control_;
  };

  bool BeginCallback() {
    const std::scoped_lock lock(mutex_);
    if (stopped_) {
      return false;
    }
    ++callbacks_in_flight_;
    return true;
  }

  void EndCallback() {
    const std::scoped_lock lock(mutex_);
    --callbacks_in_flight_;
  }

  template <typename Call>
  absl::Status Invoke(Transfer& transfer, Call call) {
    if (transfer.control->cancelled || !BeginCallback()) {
      return absl::CancelledError("the request was cancelled");
    }
    absl::Status status = call(*transfer.callbacks);
    EndCallback();
    return status;
  }

  absl::Status EnsureHead(Transfer& transfer) {
    if (transfer.head_sent) {
      return absl::OkStatus();
    }
    transfer.head_sent = true;
    HttpResponseHead head;
    head.status = transfer.status;
    head.headers = transfer.response_headers;
    head.protocol = transfer.protocol;
    head.final_url = EffectiveUrl(transfer.easy, transfer.request.url);
    return Invoke(transfer, [&](HttpTransportCallbacks& callbacks) {
      return callbacks.OnResponse(std::move(head));
    });
  }

  static size_t WriteCallback(char* data, size_t size, size_t count,
                              void* user) {
    auto* transfer = static_cast<Transfer*>(user);
    const size_t total = size * count;
    if (transfer->control->cancelled) {
      return CURL_WRITEFUNC_ERROR;
    }
    if (transfer->control->paused) {
      return CURL_WRITEFUNC_PAUSE;
    }
    const absl::Duration arrival = Now();
    Core& core = *transfer->core;
    if (!core.EnsureHead(*transfer).ok()) {
      transfer->delivery_failed = !transfer->control->cancelled;
      return CURL_WRITEFUNC_ERROR;
    }
    if (total == 0) {
      return 0;
    }
    HttpChunk chunk;
    chunk.data = SharedBytes::Copy(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data), total));
    chunk.arrival_time = arrival;
    if (!transfer->first_arrival.has_value()) {
      transfer->first_arrival = arrival;
    }
    transfer->body_bytes += total;
    const absl::Status status =
        core.Invoke(*transfer, [&](HttpTransportCallbacks& callbacks) {
          return callbacks.OnChunk(std::move(chunk));
        });
    if (!status.ok()) {
      transfer->delivery_failed = !transfer->control->cancelled;
      return CURL_WRITEFUNC_ERROR;
    }
    return total;
  }

  static size_t HeaderCallback(char* data, size_t size, size_t count,
                               void* user) {
    auto* transfer = static_cast<Transfer*>(user);
    const size_t total = size * count;
    const std::string line = Trim(std::string_view(data, total));
    if (line.empty()) {
      return total;
    }
    if (line.starts_with(kStatusLinePrefix)) {
      transfer->response_headers.clear();
      const std::string_view text(line);
      const size_t space = text.find(' ');
      transfer->protocol = ProtocolFrom(text.substr(
          kStatusLinePrefix.size(), space == std::string_view::npos
                                        ? std::string_view::npos
                                        : space - kStatusLinePrefix.size()));
      transfer->status = space == std::string_view::npos
                             ? 0
                             : StatusCodeFrom(text.substr(space + 1, 3));
      return total;
    }
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      transfer->response_headers.push_back(
          {.name = Trim(std::string_view(line).substr(0, colon)),
           .value = Trim(std::string_view(line).substr(colon + 1))});
    }
    return total;
  }

  bool Configure(Transfer& transfer) {
    const HttpRequest& request = transfer.request;
    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
      return false;
    }
    transfer.easy = easy;
    transfer.core = this;
    curl_easy_setopt(easy, CURLOPT_PRIVATE, &transfer);
    curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, transfer.error.data());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Core::WriteCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &Core::HeaderCallback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 65536L);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS,
                     static_cast<long>(request.redirects.max_redirects));
    curl_easy_setopt(
        easy, CURLOPT_REDIR_PROTOCOLS_STR,
        IsHttps(request.url) && !request.redirects.allow_https_downgrade
            ? "https"
            : "http,https");
    curl_easy_setopt(
        easy, CURLOPT_CONNECTTIMEOUT_MS,
        static_cast<long>(absl::ToInt64Milliseconds(request.connect_timeout)));
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION,
                     static_cast<long>(CURL_HTTP_VERSION_2TLS));
    curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    if (ca_bundle_.has_value()) {
      curl_easy_setopt(easy, CURLOPT_CAINFO, ca_bundle_->c_str());
    }
    bool has_encoding = false;
    for (const HttpHeader& header : request.headers) {
      has_encoding = has_encoding || Lower(header.name) == "accept-encoding";
      transfer.headers = curl_slist_append(
          transfer.headers,
          absl::StrCat(header.name, ": ", header.value).c_str());
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, transfer.headers);
    if (!has_encoding) {
      curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    }
    if (request.range.has_value()) {
      const std::string range =
          request.range->length.has_value()
              ? absl::StrCat(request.range->offset, "-",
                             request.range->offset + *request.range->length - 1)
              : absl::StrCat(request.range->offset, "-");
      curl_easy_setopt(easy, CURLOPT_RANGE, range.c_str());
    }
    switch (request.method) {
      case HttpMethod::kGet:
        break;
      case HttpMethod::kHead:
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
        break;
      case HttpMethod::kPost:
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
        break;
    }
    return true;
  }

  void Release(Transfer& transfer) {
    if (transfer.easy != nullptr) {
      curl_multi_remove_handle(multi_, transfer.easy);
      curl_easy_cleanup(transfer.easy);
      transfer.easy = nullptr;
    }
    if (transfer.headers != nullptr) {
      curl_slist_free_all(transfer.headers);
      transfer.headers = nullptr;
    }
    transfer.control->finished = true;
  }

  void Finish(Transfer& t, CURLcode result) {
    const bool cancelled = t.control->cancelled;
    if (!cancelled && !t.delivery_failed) {
      if (result == CURLE_OK) {
        Complete(t);
      } else {
        absl::Status error =
            StatusFromCurl(result, std::string_view(t.error.data()),
                           t.status >= 300 && t.status < 400);
        static_cast<void>(Invoke(t, [&](HttpTransportCallbacks& callbacks) {
          return callbacks.OnError(std::move(error));
        }));
      }
    }
    const std::shared_ptr<TransferControl> control = t.control;
    Release(t);
    std::erase_if(active_, [&t](const std::shared_ptr<Transfer>& candidate) {
      return candidate.get() == &t;
    });
    const std::scoped_lock lock(mutex_);
    std::erase(controls_, control);
  }

  void Complete(Transfer& t) {
    if (!EnsureHead(t).ok()) {
      return;
    }
    HttpMetrics metrics;
    metrics.status = t.status;
    metrics.protocol = t.protocol;
    metrics.final_url = EffectiveUrl(t.easy, t.request.url);
    metrics.request_start = t.request_start;
    metrics.completion =
        std::max(Now(), t.first_arrival.value_or(t.request_start));
    absl::Duration response_start = t.request_start;
    if (const auto first_byte =
            MicrosecondsInfo(t.easy, CURLINFO_STARTTRANSFER_TIME_T)) {
      response_start = t.request_start + *first_byte;
    }
    if (t.first_arrival.has_value()) {
      response_start = std::min(response_start, *t.first_arrival);
    }
    metrics.response_start =
        std::min(std::max(response_start, t.request_start), metrics.completion);
    metrics.body_bytes = t.body_bytes;
    const auto dns = MicrosecondsInfo(t.easy, CURLINFO_NAMELOOKUP_TIME_T);
    const auto connect = MicrosecondsInfo(t.easy, CURLINFO_CONNECT_TIME_T);
    const auto tls = MicrosecondsInfo(t.easy, CURLINFO_APPCONNECT_TIME_T);
    metrics.dns = dns;
    if (connect.has_value()) {
      metrics.connect = *connect - dns.value_or(absl::ZeroDuration());
    }
    if (tls.has_value() && connect.has_value() && *tls > *connect) {
      metrics.tls = *tls - *connect;
    }
    static_cast<void>(Invoke(t, [&](HttpTransportCallbacks& callbacks) {
      return callbacks.OnComplete(std::move(metrics));
    }));
  }

  void ApplyControls() {
    const std::vector<std::shared_ptr<Transfer>> snapshot = active_;
    for (const std::shared_ptr<Transfer>& transfer : snapshot) {
      if (transfer->control->cancelled) {
        Finish(*transfer, CURLE_ABORTED_BY_CALLBACK);
        continue;
      }
      const bool wanted = transfer->control->paused;
      if (wanted != transfer->paused_applied) {
        transfer->paused_applied = wanted;
        curl_easy_pause(transfer->easy,
                        wanted ? CURLPAUSE_RECV : CURLPAUSE_RECV_CONT);
      }
    }
  }

  void Drain() {
    int queued = 0;
    while (const CURLMsg* message = curl_multi_info_read(multi_, &queued)) {
      if (message->msg != CURLMSG_DONE) {
        continue;
      }
      CURLcode result = CURLE_OK;
      std::memcpy(&result, &message->data, sizeof(result));
      const auto found = std::ranges::find_if(active_, [&](const auto& t) {
        return t->easy == message->easy_handle;
      });
      if (found != active_.end()) {
        Finish(**found, result);
      }
    }
  }

  void Admit(const std::shared_ptr<Transfer>& transfer) {
    if (transfer->control->cancelled) {
      transfer->control->finished = true;
      const std::scoped_lock lock(mutex_);
      std::erase(controls_, transfer->control);
      return;
    }
    if (!Configure(*transfer)) {
      Release(*transfer);
      absl::Status error = absl::InternalError("curl initialisation failed");
      static_cast<void>(Invoke(*transfer, [&](HttpTransportCallbacks& cb) {
        return cb.OnError(std::move(error));
      }));
      const std::scoped_lock lock(mutex_);
      std::erase(controls_, transfer->control);
      return;
    }
    curl_multi_add_handle(multi_, transfer->easy);
    active_.push_back(transfer);
  }

  void Run() {
    for (;;) {
      std::vector<std::shared_ptr<Transfer>> added;
      {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
          return;
        }
        added.swap(pending_);
        for (const std::shared_ptr<Transfer>& transfer : added) {
          controls_.push_back(transfer->control);
        }
      }
      for (const std::shared_ptr<Transfer>& transfer : added) {
        Admit(transfer);
      }
      ApplyControls();
      int running = 0;
      curl_multi_perform(multi_, &running);
      Drain();
      int ignored = 0;
      curl_multi_poll(multi_, nullptr, 0, kPollMilliseconds, &ignored);
    }
  }

  CurlTransportOptions options_;
  std::optional<std::string> ca_bundle_;
  CURLM* multi_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<Transfer>> pending_;
  std::vector<std::shared_ptr<TransferControl>> controls_;
  std::vector<std::shared_ptr<Transfer>> active_;
  int callbacks_in_flight_ = 0;
  bool stopping_ = false;
  bool stopped_ = false;
  std::thread worker_;
};

absl::StatusOr<std::shared_ptr<CurlHttpTransport>> CurlHttpTransport::Create(
    CurlTransportOptions options) {
  InitialiseCurl();
  return std::shared_ptr<CurlHttpTransport>(
      new CurlHttpTransport(std::make_shared<Core>(std::move(options))));
}

CurlHttpTransport::CurlHttpTransport(std::shared_ptr<Core> core)
    : core_(std::move(core)) {}

CurlHttpTransport::~CurlHttpTransport() { static_cast<void>(core_->Stop()); }

absl::StatusOr<std::unique_ptr<HttpRequestHandle>> CurlHttpTransport::Start(
    HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
  return core_->Submit(std::move(request), std::move(callbacks), core_);
}

absl::Status CurlHttpTransport::Stop() { return core_->Stop(); }

}
