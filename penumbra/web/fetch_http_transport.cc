#include "penumbra/web/fetch_http_transport.h"

#include <emscripten.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"

namespace penumbra::web {
namespace {

EM_JS(void, CreateFetchTransport,
      (uintptr_t core, int credentials, int priorities), {
        globalThis.AlumbraFetchTransport.create(Module, core, credentials,
                                                priorities);
      });

EM_JS(void, StartFetchRequest,
      (uintptr_t core, uint32_t identifier, const char* request_json,
       const uint8_t* body, size_t body_size),
      {
        globalThis.AlumbraFetchTransport.start(Module, core, identifier,
                                               request_json, body, body_size);
      });

EM_JS(void, ControlFetchRequest,
      (uintptr_t core, uint32_t identifier, int action), {
        globalThis.AlumbraFetchTransport.control(Module, core, identifier,
                                                 action);
      });

EM_JS(void, DestroyFetchTransport, (uintptr_t core),
      { globalThis.AlumbraFetchTransport.destroy(Module, core); });

std::string JsonString(std::string_view value) {
  std::string result = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20U) {
          result += "\\u00";
          result.push_back("0123456789abcdef"[character >> 4U]);
          result.push_back("0123456789abcdef"[character & 0x0fU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
  }
  result.push_back('"');
  return result;
}

std::string MethodName(HttpMethod method) {
  switch (method) {
    case HttpMethod::kGet:
      return "GET";
    case HttpMethod::kHead:
      return "HEAD";
    case HttpMethod::kPost:
      return "POST";
  }
  return "GET";
}

std::string PriorityName(RequestPriority priority) {
  switch (priority) {
    case RequestPriority::kLow:
      return "low";
    case RequestPriority::kNormal:
      return "normal";
    case RequestPriority::kHigh:
      return "high";
  }
  return "normal";
}

std::string TakeString(char* value) {
  if (value == nullptr) {
    return {};
  }
  std::string result(value);
  std::free(value);
  return result;
}

std::string RequestJson(const HttpRequest& request,
                        FetchTransportOptions options) {
  std::string json = absl::StrCat(
      "{\"url\":", JsonString(request.url),
      ",\"method\":", JsonString(MethodName(request.method)), ",\"headers\":[");
  bool first = true;
  for (const HttpHeader& header : request.headers) {
    if (!first) {
      json.push_back(',');
    }
    first = false;
    json += absl::StrCat("[", JsonString(header.name), ",",
                         JsonString(header.value), "]");
  }
  json += "],\"range\":";
  if (request.range.has_value()) {
    json += absl::StrCat("{\"offset\":", request.range->offset, ",\"length\":");
    json += request.range->length.has_value()
                ? absl::StrCat(*request.range->length)
                : "null";
    json.push_back('}');
  } else {
    json += "null";
  }
  const FetchCredentials credentials = request.include_credentials
                                           ? FetchCredentials::kInclude
                                           : options.credentials;
  const int64_t timeout_ms =
      std::max<int64_t>(1, absl::ToInt64Milliseconds(request.connect_timeout));
  return absl::StrCat(
      json, ",\"includeCredentials\":",
      credentials == FetchCredentials::kInclude ? "true" : "false",
      ",\"connectTimeoutMs\":", timeout_ms,
      ",\"maxRedirects\":", request.redirects.max_redirects,
      ",\"allowHttpsDowngrade\":",
      request.redirects.allow_https_downgrade ? "true" : "false",
      ",\"priority\":", JsonString(PriorityName(request.priority)), "}");
}
}

class FetchHttpTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  struct Transfer {
    std::shared_ptr<Core> owner;
    std::shared_ptr<HttpTransportCallbacks> callbacks;
    HttpResponseHead head;
    std::atomic<bool> cancelled = false;
    std::atomic<bool> terminal = false;
  };

  explicit Core(FetchTransportOptions options) : options_(options) {
    CreateFetchTransport(reinterpret_cast<uintptr_t>(this),
                         static_cast<int>(options.credentials),
                         options.use_priority_hints ? 1 : 0);
  }

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
    if (callbacks == nullptr || request.url.empty() ||
        request.connect_timeout <= absl::ZeroDuration() ||
        request.redirects.max_redirects < 0 ||
        request.redirects.max_redirects > 20 ||
        (request.range.has_value() && request.range->length == 0) ||
        (request.range.has_value() && request.range->length.has_value() &&
         *request.range->length - 1 >
             std::numeric_limits<uint64_t>::max() - request.range->offset) ||
        (request.method != HttpMethod::kPost && !request.body.empty())) {
      return absl::InvalidArgumentError("invalid HTTP request");
    }
    auto transfer = std::make_shared<Transfer>();
    transfer->owner = shared_from_this();
    transfer->callbacks = std::move(callbacks);
    uint32_t identifier = 0;
    {
      const std::scoped_lock lock(mutex_);
      if (stopped_) {
        return absl::FailedPreconditionError("transport is stopped");
      }
      identifier = next_identifier_++;
      if (identifier == 0) {
        identifier = next_identifier_++;
      }
      transfers_.emplace(identifier, transfer);
    }
    const std::string json = RequestJson(request, options_);
    StartFetchRequest(reinterpret_cast<uintptr_t>(this), identifier,
                      json.c_str(), request.body.data(), request.body.size());
    return std::make_unique<Handle>(shared_from_this(), identifier);
  }

  absl::Status Stop() {
    std::vector<uint32_t> identifiers;
    {
      const std::scoped_lock lock(mutex_);
      stopped_ = true;
      for (const auto& [identifier, transfer] : transfers_) {
        transfer->cancelled.store(true);
        identifiers.push_back(identifier);
      }
    }
    for (uint32_t identifier : identifiers) {
      ControlFetchRequest(reinterpret_cast<uintptr_t>(this), identifier, 0);
    }
    if (!identifiers.empty()) {
      return absl::UnavailableError("a fetch request is still active");
    }
    DestroyFetchTransport(reinterpret_cast<uintptr_t>(this));
    return absl::OkStatus();
  }

  void Cancel(uint32_t identifier) {
    std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->terminal.load()) {
      return;
    }
    transfer->cancelled.store(true);
    ControlFetchRequest(reinterpret_cast<uintptr_t>(this), identifier, 0);
  }

  absl::Status Pause(uint32_t identifier) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load() ||
        transfer->terminal.load()) {
      return absl::FailedPreconditionError("request is no longer active");
    }
    ControlFetchRequest(reinterpret_cast<uintptr_t>(this), identifier, 1);
    return absl::OkStatus();
  }

  absl::Status Resume(uint32_t identifier) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load() ||
        transfer->terminal.load()) {
      return absl::FailedPreconditionError("request is no longer active");
    }
    ControlFetchRequest(reinterpret_cast<uintptr_t>(this), identifier, 2);
    return absl::OkStatus();
  }

  void ResponseBegin(uint32_t identifier, int status, int protocol,
                     std::string url) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      return;
    }
    transfer->head.status = status;
    transfer->head.protocol = static_cast<HttpProtocol>(protocol);
    transfer->head.final_url = std::move(url);
  }

  void ResponseHeader(uint32_t identifier, std::string name,
                      std::string value) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      return;
    }
    transfer->head.headers.push_back(
        {.name = std::move(name), .value = std::move(value)});
  }

  void ResponseEnd(uint32_t identifier) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      return;
    }
    const absl::Status status = transfer->callbacks->OnResponse(transfer->head);
    if (!status.ok()) {
      Cancel(identifier);
    }
  }

  void Chunk(uint32_t identifier, uint8_t* data, size_t size, double arrival) {
    std::shared_ptr<const void> owner(
        data, [](const void* bytes) { std::free(const_cast<void*>(bytes)); });
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      return;
    }
    const auto bytes = SharedBytes::Wrap(std::move(owner),
                                         std::span<const uint8_t>(data, size));
    const absl::Status status = transfer->callbacks->OnChunk(
        {.data = bytes, .arrival_time = absl::Milliseconds(arrival)});
    if (!status.ok()) {
      Cancel(identifier);
    }
  }

  void Complete(uint32_t identifier, int status, double start, double response,
                double completion, uint64_t body_bytes, int protocol,
                bool coarse_timing) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load() ||
        transfer->terminal.exchange(true)) {
      return;
    }
    HttpMetrics metrics;
    metrics.status = status;
    metrics.protocol = static_cast<HttpProtocol>(protocol);
    metrics.final_url = transfer->head.final_url;
    metrics.request_start = absl::Milliseconds(start);
    metrics.response_start = absl::Milliseconds(response);
    metrics.completion = absl::Milliseconds(completion);
    metrics.body_bytes = body_bytes;
    metrics.coarse_timing = coarse_timing;
    static_cast<void>(transfer->callbacks->OnComplete(std::move(metrics)));
  }

  void Error(uint32_t identifier, int error_code, std::string detail) {
    const std::shared_ptr<Transfer> transfer = Find(identifier);
    if (transfer == nullptr || transfer->cancelled.load() ||
        transfer->terminal.exchange(true)) {
      return;
    }
    absl::Status error = absl::UnavailableError(detail);
    if (error_code == 1) {
      error = absl::DeadlineExceededError(detail);
    } else if (error_code == 2) {
      error = absl::FailedPreconditionError(detail);
    }
    static_cast<void>(transfer->callbacks->OnError(error));
  }

  void Ended(uint32_t identifier) {
    bool destroy_transport = false;
    {
      const std::scoped_lock lock(mutex_);
      transfers_.erase(identifier);
      destroy_transport = stopped_ && transfers_.empty();
    }
    if (destroy_transport) {
      DestroyFetchTransport(reinterpret_cast<uintptr_t>(this));
    }
  }

 private:
  class Handle final : public HttpRequestHandle {
   public:
    Handle(std::shared_ptr<Core> core, uint32_t identifier)
        : core_(std::move(core)), identifier_(identifier) {}
    ~Handle() override { Cancel(); }
    void Cancel() override { core_->Cancel(identifier_); }
    absl::Status Pause() override { return core_->Pause(identifier_); }
    absl::Status Resume() override { return core_->Resume(identifier_); }

   private:
    std::shared_ptr<Core> core_;
    uint32_t identifier_;
  };

  std::shared_ptr<Transfer> Find(uint32_t identifier) {
    const std::scoped_lock lock(mutex_);
    const auto found = transfers_.find(identifier);
    return found == transfers_.end() ? nullptr : found->second;
  }

  FetchTransportOptions options_;
  std::mutex mutex_;
  std::map<uint32_t, std::shared_ptr<Transfer>> transfers_;
  uint32_t next_identifier_ = 1;
  bool stopped_ = false;
};

extern "C" {

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_response_begin(
    uintptr_t core, uint32_t identifier, int status, int protocol,
    const char* final_url) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->ResponseBegin(
      identifier, status, protocol, TakeString(const_cast<char*>(final_url)));
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_response_header(uintptr_t core,
                                                           uint32_t identifier,
                                                           const char* name,
                                                           const char* value) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->ResponseHeader(
      identifier, TakeString(const_cast<char*>(name)),
      TakeString(const_cast<char*>(value)));
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_response_end(uintptr_t core,
                                                        uint32_t identifier) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->ResponseEnd(identifier);
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_chunk(uintptr_t core,
                                                 uint32_t identifier,
                                                 uint8_t* data, size_t size,
                                                 double arrival) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->Chunk(identifier, data,
                                                           size, arrival);
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_complete(
    uintptr_t core, uint32_t identifier, int status, double start,
    double response, double completion, uint64_t body_bytes, int protocol,
    int coarse_timing) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->Complete(
      identifier, status, start, response, completion, body_bytes, protocol,
      coarse_timing != 0);
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_error(uintptr_t core,
                                                 uint32_t identifier,
                                                 int timeout,
                                                 const char* detail) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->Error(
      identifier, timeout,
      detail == nullptr ? "fetch failed"
                        : TakeString(const_cast<char*>(detail)));
}

EMSCRIPTEN_KEEPALIVE void alumbra_web_http_ended(uintptr_t core,
                                                 uint32_t identifier) {
  reinterpret_cast<FetchHttpTransport::Core*>(core)->Ended(identifier);
}
}

absl::StatusOr<std::shared_ptr<FetchHttpTransport>> FetchHttpTransport::Create(
    FetchTransportOptions options) {
  if (static_cast<int>(options.credentials) < 0 ||
      static_cast<int>(options.credentials) > 2) {
    return absl::InvalidArgumentError("invalid fetch credentials mode");
  }
  return std::shared_ptr<FetchHttpTransport>(
      new FetchHttpTransport(std::make_shared<Core>(options)));
}

FetchHttpTransport::FetchHttpTransport(std::shared_ptr<Core> core)
    : core_(std::move(core)) {}

FetchHttpTransport::~FetchHttpTransport() { static_cast<void>(core_->Stop()); }

absl::StatusOr<std::unique_ptr<HttpRequestHandle>> FetchHttpTransport::Start(
    HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
  return core_->Start(std::move(request), std::move(callbacks));
}

absl::Status FetchHttpTransport::Stop() { return core_->Stop(); }
}
