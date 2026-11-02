#include "umbra/source/http_byte_stream_provider.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/status.h"
#include "umbra/base/tick_clock.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"
#include "umbra/source/credentials.h"
#include "umbra/source/sequenced_receiver.h"
#include "umbra/source/source_error.h"
#include "umbra/source/url.h"

namespace umbra::source {
namespace {

using internal::HttpProviderContext;

std::string Lower(std::string_view text) {
  std::string result(text);
  std::ranges::transform(result, result.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return result;
}

bool IsSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && IsSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && IsSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> FindHeaders(
    const std::vector<penumbra::HttpHeader>& headers, std::string_view name) {
  std::vector<std::string_view> values;
  const std::string wanted = Lower(name);
  for (const penumbra::HttpHeader& header : headers) {
    if (Lower(header.name) == wanted) {
      values.push_back(Trim(header.value));
    }
  }
  return values;
}

std::optional<std::string_view> FindHeader(
    const std::vector<penumbra::HttpHeader>& headers, std::string_view name) {
  const std::vector<std::string_view> values = FindHeaders(headers, name);
  if (values.empty()) {
    return std::nullopt;
  }
  return values.front();
}

std::optional<uint64_t> ParseNumber(std::string_view text) {
  text = Trim(text);
  if (text.empty()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    const auto digit = static_cast<uint64_t>(c - '0');
    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    value = (value * 10) + digit;
  }
  return value;
}

struct ContentRange {
  std::optional<uint64_t> first;
  std::optional<uint64_t> last;
  std::optional<uint64_t> total;
};

std::optional<ContentRange> ParseContentRange(std::string_view text) {
  text = Trim(text);
  if (!Lower(text.substr(0, 6)).starts_with("bytes ")) {
    return std::nullopt;
  }
  text = Trim(text.substr(6));
  const size_t slash = text.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view span = Trim(text.substr(0, slash));
  const std::string_view total = Trim(text.substr(slash + 1));
  ContentRange range;
  if (total != "*") {
    range.total = ParseNumber(total);
    if (!range.total.has_value()) {
      return std::nullopt;
    }
  }
  if (span != "*") {
    const size_t dash = span.find('-');
    if (dash == std::string_view::npos) {
      return std::nullopt;
    }
    range.first = ParseNumber(span.substr(0, dash));
    range.last = ParseNumber(span.substr(dash + 1));
    if (!range.first.has_value() || !range.last.has_value() ||
        *range.last < *range.first) {
      return std::nullopt;
    }
  }
  return range;
}

bool AcceptsByteRanges(const std::vector<penumbra::HttpHeader>& headers) {
  const std::vector<std::string_view> values =
      FindHeaders(headers, "accept-ranges");
  return std::ranges::any_of(values, [](std::string_view value) {
    return Lower(value).find("bytes") != std::string::npos;
  });
}

std::string QuotedOrToken(std::string_view value, size_t start) {
  std::string result;
  if (start < value.size() && value[start] == '"') {
    ++start;
    while (start < value.size() && value[start] != '"') {
      if (value[start] == '\\' && start + 1 < value.size()) {
        ++start;
      }
      result.push_back(value[start]);
      ++start;
    }
    return result;
  }
  while (start < value.size() && value[start] != ',' && value[start] != ' ') {
    result.push_back(value[start]);
    ++start;
  }
  return result;
}

std::optional<size_t> FindBasicScheme(const std::string& lowered) {
  size_t position = 0;
  while (position < lowered.size()) {
    const size_t found = lowered.find("basic", position);
    if (found == std::string::npos) {
      return std::nullopt;
    }
    const size_t after = found + 5;
    const bool at_start =
        found == 0 || lowered[found - 1] == ',' || lowered[found - 1] == ' ';
    const bool whole = after == lowered.size() || lowered[after] == ' ' ||
                       lowered[after] == ',';
    if (at_start && whole) {
      return after;
    }
    position = after;
  }
  return std::nullopt;
}

std::optional<std::string> BasicRealm(
    const std::vector<penumbra::HttpHeader>& headers, bool& offered) {
  offered = false;
  for (const std::string_view value :
       FindHeaders(headers, "www-authenticate")) {
    const std::string lowered = Lower(value);
    const std::optional<size_t> after = FindBasicScheme(lowered);
    if (!after.has_value()) {
      continue;
    }
    offered = true;
    const size_t realm = lowered.find("realm=", *after);
    if (realm == std::string::npos) {
      return std::string();
    }
    return QuotedOrToken(value, realm + 6);
  }
  return std::nullopt;
}

bool CarriesMedia(penumbra::RequestClass request_class) {
  return request_class == penumbra::RequestClass::kMedia ||
         request_class == penumbra::RequestClass::kInit ||
         request_class == penumbra::RequestClass::kPrefetch;
}

std::string StripUserInfo(const std::string& url) {
  absl::StatusOr<UrlReference> parsed = UrlReference::ParseAbsolute(url);
  if (!parsed.ok() || !parsed->userinfo.has_value()) {
    return url;
  }
  return parsed->WithoutUserInfo().ToString();
}

class HttpByteStream;

class TransportReceiver final : public penumbra::HttpTransportCallbacks {
 public:
  explicit TransportReceiver(
      std::shared_ptr<SequencedReceiver<HttpByteStream>> receiver)
      : receiver_(std::move(receiver)) {}

  absl::Status OnResponse(penumbra::HttpResponseHead head) override;
  absl::Status OnChunk(penumbra::HttpChunk chunk) override;
  absl::Status OnComplete(penumbra::HttpMetrics metrics) override;
  absl::Status OnError(absl::Status error) override;

 private:
  std::shared_ptr<SequencedReceiver<HttpByteStream>> receiver_;
};

class HttpByteStream final : public penumbra::ByteStream {
 public:
  static absl::StatusOr<std::unique_ptr<HttpByteStream>> Start(
      std::shared_ptr<HttpProviderContext> context,
      penumbra::ByteStreamRequest request,
      std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) {
    std::unique_ptr<HttpByteStream> stream(new HttpByteStream(
        std::move(context), std::move(request), std::move(callbacks)));
    const absl::Status status = stream->StartTransport();
    if (!status.ok()) {
      return stream->MapError(status);
    }
    return stream;
  }

  HttpByteStream(const HttpByteStream&) = delete;
  HttpByteStream& operator=(const HttpByteStream&) = delete;
  ~HttpByteStream() override { Cancel(); }

  void Cancel() override {
    if (cancelled_) {
      return;
    }
    cancelled_ = true;
    Finish();
  }

  absl::Status Pause() override {
    if (done_) {
      return absl::FailedPreconditionError("the request is not active");
    }
    if (paused_) {
      return absl::OkStatus();
    }
    if (handle_ != nullptr) {
      const absl::Status status = handle_->Pause();
      if (!status.ok()) {
        return status;
      }
    }
    paused_ = true;
    paused_since_ = Now();
    ++timer_generation_;
    return absl::OkStatus();
  }

  absl::Status Resume() override {
    if (done_) {
      return absl::FailedPreconditionError("the request is not active");
    }
    if (!paused_) {
      return absl::OkStatus();
    }
    if (handle_ != nullptr) {
      const absl::Status status = handle_->Resume();
      if (!status.ok()) {
        return status;
      }
    }
    paused_ = false;
    paused_total_ += Now() - paused_since_;
    if (!awaiting_credential_) {
      ArmTimer(Options().read_timeout);
    }
    return absl::OkStatus();
  }

  void HandleResponse(const penumbra::HttpResponseHead& head) {
    if (done_) {
      return;
    }
    ArmTimerIfActive(Options().read_timeout);
    if (head.status == 401) {
      HandleChallenge(head);
      return;
    }
    if (head.status == 416) {
      HandleUnsatisfiableRange(head);
      return;
    }
    if (head.status != 200 && head.status != 206) {
      FailStatus(head);
      return;
    }
    const std::string final_url =
        StripUserInfo(head.final_url.empty() ? request_.url : head.final_url);
    penumbra::ByteStreamInfo info;
    info.final_url = final_url;
    if (const auto type = FindHeader(head.headers, "content-type")) {
      info.content_type = std::string(*type);
    }
    if (head.status == 206) {
      const auto header = FindHeader(head.headers, "content-range");
      const std::optional<ContentRange> range =
          header.has_value() ? ParseContentRange(*header) : std::nullopt;
      if (!range.has_value() || range->first != request_.offset) {
        Fail(MakeStatusError(base::ErrorCode::kNetworkRequestFailed,
                             absl::StatusCode::kUnavailable,
                             "the server answered a range request with an "
                             "unexpected Content-Range",
                             head.status));
        return;
      }
      info.seekable = true;
      info.size = range->total;
    } else {
      const auto length = FindHeader(head.headers, "content-length");
      const auto encoding = FindHeader(head.headers, "content-encoding");
      if (length.has_value() &&
          (!encoding.has_value() || Lower(*encoding) == "identity")) {
        info.size = ParseNumber(*length);
      }
      info.seekable = AcceptsByteRanges(head.headers);
      skip_ = request_.offset;
    }
    remaining_ = request_.length;
    if (credential_sent_ && credential_.has_value()) {
      context_->credentials_by_origin[Origin()] = *credential_;
    }
    responded_ = true;
    static_cast<void>(callbacks_->OnOpen(std::move(info)));
  }

  void HandleChunk(penumbra::HttpChunk chunk) {
    if (done_ || !responded_) {
      return;
    }
    ArmTimerIfActive(Options().read_timeout);
    penumbra::SharedBytes data = std::move(chunk.data);
    if (skip_ > 0) {
      if (data.size() <= skip_) {
        skip_ -= data.size();
        return;
      }
      data = data.Slice(static_cast<size_t>(skip_),
                        data.size() - static_cast<size_t>(skip_))
                 .value_or(penumbra::SharedBytes());
      skip_ = 0;
    }
    bool last = false;
    if (remaining_.has_value()) {
      uint64_t left = *remaining_;
      if (data.size() >= left) {
        data = data.Slice(0, static_cast<size_t>(left))
                   .value_or(penumbra::SharedBytes());
        last = true;
      }
      left -= data.size();
      remaining_ = left;
    }
    if (!data.empty()) {
      penumbra::ByteStreamChunk out;
      out.data = std::move(data);
      out.arrival_time = chunk.arrival_time;
      static_cast<void>(callbacks_->OnData(std::move(out)));
    }
    if (last) {
      Finish();
      static_cast<void>(callbacks_->OnEnd());
    }
  }

  void HandleComplete(const penumbra::HttpMetrics& metrics) {
    if (done_) {
      return;
    }
    Finish();
    if (!responded_) {
      static_cast<void>(callbacks_->OnError(MakeStatusError(
          base::ErrorCode::kNetworkRequestFailed,
          absl::StatusCode::kUnavailable,
          "the transport finished without a response", metrics.status)));
      return;
    }
    if (context_->dependencies.observer != nullptr) {
      HttpRequestReport report;
      report.url = StripUserInfo(metrics.final_url.empty() ? request_.url
                                                           : metrics.final_url);
      report.request_class = request_.request_class;
      report.status = metrics.status;
      report.protocol = metrics.protocol;
      report.body_bytes = metrics.body_bytes;
      report.time_to_first_byte =
          metrics.response_start - metrics.request_start;
      report.paused_duration = paused_total_;
      report.transfer_duration = std::max(
          absl::ZeroDuration(),
          (metrics.completion - metrics.response_start) - paused_total_);
      report.coarse_timing = metrics.coarse_timing;
      context_->dependencies.observer->OnRequestComplete(report);
    }
    static_cast<void>(callbacks_->OnEnd());
  }

  void HandleError(const absl::Status& error) {
    if (done_) {
      return;
    }
    Finish();
    static_cast<void>(callbacks_->OnError(MapError(error)));
  }

  void HandleCredential(std::optional<penumbra::Credential> credential) {
    if (done_ || !awaiting_credential_) {
      return;
    }
    awaiting_credential_ = false;
    if (!credential.has_value()) {
      Fail(MakeStatusError(base::ErrorCode::kNetworkAccessDenied,
                           absl::StatusCode::kPermissionDenied,
                           "the server requires credentials and none were "
                           "provided",
                           401));
      return;
    }
    credential_ = *std::move(credential);
    credential_sent_ = true;
    const absl::Status status = StartTransport();
    if (!status.ok()) {
      Fail(MapError(status));
    }
  }

  void HandleTimer(uint64_t generation) {
    if (done_ || paused_ || awaiting_credential_ ||
        generation != timer_generation_) {
      return;
    }
    const std::string seconds =
        absl::StrCat(absl::ToInt64Milliseconds(Options().read_timeout), " ms");
    Fail(MakeStatusError(base::ErrorCode::kNetworkTimeout,
                         absl::StatusCode::kUnavailable,
                         absl::StrCat("no data arrived for ", seconds), 0));
  }

 private:
  HttpByteStream(std::shared_ptr<HttpProviderContext> context,
                 penumbra::ByteStreamRequest request,
                 std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks)
      : context_(std::move(context)),
        request_(std::move(request)),
        callbacks_(std::move(callbacks)),
        credential_(request_.credential) {
    control_ = SequencedReceiver<HttpByteStream>::Create(
        context_->dependencies.runner, this);
  }

  [[nodiscard]] const HttpByteStreamOptions& Options() const {
    return context_->options;
  }

  [[nodiscard]] absl::Duration Now() const {
    return context_->dependencies.clock->NowTicks() - base::TimeTicks();
  }

  [[nodiscard]] std::string Origin() const {
    absl::StatusOr<UrlReference> parsed =
        UrlReference::ParseAbsolute(request_.url);
    return parsed.ok() ? parsed->Origin() : std::string();
  }

  [[nodiscard]] absl::Status MakeStatusError(base::ErrorCode code,
                                             absl::StatusCode status_code,
                                             std::string_view message,
                                             int http_status) const {
    std::map<std::string, std::string> context = {{"url", request_.url}};
    if (http_status != 0) {
      context["http_status"] = absl::StrCat(http_status);
    }
    return MakeSourceError(code, status_code, message, std::move(context),
                           *context_->dependencies.redactor);
  }

  [[nodiscard]] absl::Status MapError(const absl::Status& error) const {
    const absl::StatusOr<std::optional<base::ErrorDetail>> detail =
        base::GetErrorDetail(error);
    if (detail.ok() && detail->has_value()) {
      return error;
    }
    switch (error.code()) {
      case absl::StatusCode::kCancelled:
        return error;
      case absl::StatusCode::kDeadlineExceeded:
        return MakeStatusError(base::ErrorCode::kNetworkTimeout,
                               absl::StatusCode::kUnavailable, error.message(),
                               0);
      case absl::StatusCode::kUnauthenticated:
        return MakeStatusError(base::ErrorCode::kNetworkTlsFailed,
                               absl::StatusCode::kUnavailable, error.message(),
                               0);
      case absl::StatusCode::kFailedPrecondition:
        return MakeStatusError(base::ErrorCode::kNetworkRedirectRefused,
                               absl::StatusCode::kFailedPrecondition,
                               error.message(), 0);
      case absl::StatusCode::kInvalidArgument:
        return MakeStatusError(base::ErrorCode::kNetworkUrlInvalid,
                               absl::StatusCode::kInvalidArgument,
                               error.message(), 0);
      default:
        return MakeStatusError(base::ErrorCode::kNetworkRequestFailed,
                               absl::StatusCode::kUnavailable, error.message(),
                               0);
    }
  }

  [[nodiscard]] penumbra::HttpRequest BuildRequest() const {
    penumbra::HttpRequest http;
    http.method = penumbra::HttpMethod::kGet;
    http.url = request_.url;
    http.request_class = request_.request_class;
    http.priority = request_.request_class == penumbra::RequestClass::kPrefetch
                        ? penumbra::RequestPriority::kLow
                        : penumbra::RequestPriority::kNormal;
    http.include_credentials = false;
    http.connect_timeout = Options().connect_timeout;
    http.redirects.max_redirects = Options().max_redirects;
    http.redirects.allow_https_downgrade =
        Options().allow_https_to_http_redirect;
    http.headers.push_back({.name = "User-Agent",
                            .value = Options().user_agent.empty()
                                         ? std::string(kDefaultUserAgent)
                                         : Options().user_agent});
    if (CarriesMedia(request_.request_class)) {
      http.headers.push_back({.name = "Accept-Encoding", .value = "identity"});
    }
    for (const penumbra::HttpHeader& header : Options().headers) {
      http.headers.push_back(header);
    }
    std::optional<penumbra::Credential> credential;
    if (credential_sent_) {
      credential = credential_;
    } else {
      const auto cached = context_->credentials_by_origin.find(Origin());
      if (cached != context_->credentials_by_origin.end()) {
        credential = cached->second;
      }
    }
    if (credential.has_value()) {
      http.headers.push_back(
          {.name = "Authorization",
           .value = absl::StrCat("Basic ", absl::Base64Escape(absl::StrCat(
                                               credential->username, ":",
                                               credential->password)))});
    }
    if (request_.offset > 0 || request_.length.has_value() ||
        CarriesMedia(request_.request_class)) {
      http.range = penumbra::ByteRange{.offset = request_.offset,
                                       .length = request_.length};
    }
    return http;
  }

  absl::Status StartTransport() {
    if (transport_ != nullptr) {
      transport_->Cancel();
      transport_.reset();
    }
    handle_.reset();
    transport_ = SequencedReceiver<HttpByteStream>::Create(
        context_->dependencies.runner, this);
    absl::StatusOr<std::unique_ptr<penumbra::HttpRequestHandle>> handle =
        context_->dependencies.transport->Start(
            BuildRequest(), std::make_shared<TransportReceiver>(transport_));
    if (!handle.ok()) {
      transport_->Cancel();
      transport_.reset();
      return handle.status();
    }
    handle_ = *std::move(handle);
    if (paused_) {
      static_cast<void>(handle_->Pause());
    } else {
      ArmTimer(Options().connect_timeout + Options().read_timeout);
    }
    return absl::OkStatus();
  }

  void ArmTimer(absl::Duration delay) {
    const uint64_t generation = ++timer_generation_;
    static_cast<void>(control_->PostDelayed(
        [generation](HttpByteStream& stream) {
          stream.HandleTimer(generation);
        },
        delay));
  }

  void ArmTimerIfActive(absl::Duration delay) {
    if (!paused_ && !awaiting_credential_) {
      ArmTimer(delay);
    }
  }

  void Finish() {
    done_ = true;
    ++timer_generation_;
    if (transport_ != nullptr) {
      transport_->Cancel();
      transport_.reset();
    }
    handle_.reset();
    if (control_ != nullptr) {
      control_->Cancel();
      control_.reset();
    }
  }

  void Fail(absl::Status error) {
    Finish();
    static_cast<void>(callbacks_->OnError(std::move(error)));
  }

  void HandleChallenge(const penumbra::HttpResponseHead& head) {
    bool offered = false;
    const std::optional<std::string> realm = BasicRealm(head.headers, offered);
    const absl::Status denied = MakeStatusError(
        base::ErrorCode::kNetworkAccessDenied,
        absl::StatusCode::kPermissionDenied,
        offered ? "the server rejected the credentials"
                : "the server requires an authentication scheme that is not "
                  "supported",
        401);
    if (!offered || credential_sent_) {
      if (credential_sent_) {
        context_->credentials_by_origin.erase(Origin());
      }
      Fail(denied);
      return;
    }
    if (transport_ != nullptr) {
      transport_->Cancel();
      transport_.reset();
    }
    handle_.reset();
    ++timer_generation_;
    if (credential_.has_value()) {
      credential_sent_ = true;
      const absl::Status status = StartTransport();
      if (!status.ok()) {
        Fail(MapError(status));
      }
      return;
    }
    const auto cached = context_->credentials_by_origin.find(Origin());
    if (cached != context_->credentials_by_origin.end()) {
      credential_ = cached->second;
      credential_sent_ = true;
      const absl::Status status = StartTransport();
      if (!status.ok()) {
        Fail(MapError(status));
      }
      return;
    }
    if (context_->dependencies.credentials == nullptr) {
      Fail(denied);
      return;
    }
    awaiting_credential_ = true;
    const CredentialChallenge challenge{
        .origin = Origin(), .scheme = "Basic", .realm = realm.value_or("")};
    context_->dependencies.credentials->RequestCredential(
        challenge,
        [control = control_](std::optional<penumbra::Credential> credential) {
          static_cast<void>(control->Post([credential = std::move(credential)](
                                              HttpByteStream& stream) mutable {
            stream.HandleCredential(std::move(credential));
          }));
        });
  }

  void HandleUnsatisfiableRange(const penumbra::HttpResponseHead& head) {
    const auto header = FindHeader(head.headers, "content-range");
    const std::optional<ContentRange> range =
        header.has_value() ? ParseContentRange(*header) : std::nullopt;
    if (range.has_value() && range->total.has_value() &&
        request_.offset >= *range->total) {
      penumbra::ByteStreamInfo info;
      info.seekable = true;
      info.size = range->total;
      info.final_url =
          StripUserInfo(head.final_url.empty() ? request_.url : head.final_url);
      responded_ = true;
      Finish();
      static_cast<void>(callbacks_->OnOpen(std::move(info)));
      static_cast<void>(callbacks_->OnEnd());
      return;
    }
    Fail(MakeStatusError(base::ErrorCode::kNetworkRangeUnsatisfiable,
                         absl::StatusCode::kOutOfRange,
                         "the server cannot satisfy the requested byte range",
                         416));
  }

  void FailStatus(const penumbra::HttpResponseHead& head) {
    const int status = head.status;
    std::map<std::string, std::string> context = {
        {"url", request_.url}, {"http_status", absl::StrCat(status)}};
    if (const auto retry = FindHeader(head.headers, "retry-after")) {
      context["retry_after"] = std::string(*retry);
    }
    base::ErrorCode code = base::ErrorCode::kNetworkRequestFailed;
    absl::StatusCode canonical = absl::StatusCode::kUnavailable;
    if (status == 404 || status == 410) {
      code = base::ErrorCode::kSourceNotFound;
      canonical = absl::StatusCode::kNotFound;
    } else if (status == 403) {
      code = base::ErrorCode::kNetworkAccessDenied;
      canonical = absl::StatusCode::kPermissionDenied;
    } else if (status == 408 || status == 429 || status >= 500) {
      code = base::ErrorCode::kNetworkServerError;
    }
    Fail(MakeSourceError(code, canonical,
                         absl::StrCat("the server answered HTTP ", status),
                         std::move(context), *context_->dependencies.redactor));
  }

  std::shared_ptr<HttpProviderContext> context_;
  penumbra::ByteStreamRequest request_;
  std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks_;
  std::shared_ptr<SequencedReceiver<HttpByteStream>> control_;
  std::shared_ptr<SequencedReceiver<HttpByteStream>> transport_;
  std::unique_ptr<penumbra::HttpRequestHandle> handle_;
  std::optional<penumbra::Credential> credential_;
  std::optional<uint64_t> remaining_;
  uint64_t skip_ = 0;
  uint64_t timer_generation_ = 0;
  absl::Duration paused_since_;
  absl::Duration paused_total_;
  bool credential_sent_ = false;
  bool awaiting_credential_ = false;
  bool responded_ = false;
  bool paused_ = false;
  bool cancelled_ = false;
  bool done_ = false;
};

absl::Status TransportReceiver::OnResponse(penumbra::HttpResponseHead head) {
  return receiver_->Post([head = std::move(head)](HttpByteStream& stream) {
    stream.HandleResponse(head);
  });
}

absl::Status TransportReceiver::OnChunk(penumbra::HttpChunk chunk) {
  return receiver_->Post(
      [chunk = std::move(chunk)](HttpByteStream& stream) mutable {
        stream.HandleChunk(std::move(chunk));
      });
}

absl::Status TransportReceiver::OnComplete(penumbra::HttpMetrics metrics) {
  return receiver_->Post(
      [metrics = std::move(metrics)](HttpByteStream& stream) {
        stream.HandleComplete(metrics);
      });
}

absl::Status TransportReceiver::OnError(absl::Status error) {
  return receiver_->Post([error = std::move(error)](HttpByteStream& stream) {
    stream.HandleError(error);
  });
}

}

HttpByteStreamOptions HttpByteStreamOptionsFromSettings(
    const settings::Snapshot& snapshot) {
  HttpByteStreamOptions options;
  options.connect_timeout =
      snapshot.Get(settings::kNetworkHttpConnectTimeoutMs);
  options.read_timeout = snapshot.Get(settings::kNetworkHttpReadTimeoutMs);
  options.max_redirects =
      static_cast<int>(snapshot.Get(settings::kNetworkHttpMaxRedirects));
  options.allow_https_to_http_redirect =
      snapshot.Get(settings::kNetworkHttpAllowHttpsToHttpRedirect);
  options.user_agent = snapshot.Get(settings::kNetworkHttpUserAgent);
  return options;
}

absl::StatusOr<std::shared_ptr<HttpByteStreamProvider>>
HttpByteStreamProvider::Create(HttpByteStreamDependencies dependencies,
                               HttpByteStreamOptions options) {
  if (dependencies.transport == nullptr || dependencies.runner == nullptr ||
      dependencies.redactor == nullptr) {
    return absl::InvalidArgumentError(
        "a transport, a task runner and a redactor are required");
  }
  if (dependencies.clock == nullptr) {
    dependencies.clock = &base::GetDefaultTickClock();
  }
  if (options.connect_timeout <= absl::ZeroDuration() ||
      options.read_timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError("timeouts must be positive");
  }
  if (options.max_redirects < 0 || options.max_redirects > kMaxRedirectLimit) {
    return absl::InvalidArgumentError("max redirects must be between 0 and 20");
  }
  auto context = std::make_shared<HttpProviderContext>();
  context->dependencies = std::move(dependencies);
  context->options = std::move(options);
  return std::shared_ptr<HttpByteStreamProvider>(
      new HttpByteStreamProvider(std::move(context)));
}

std::vector<std::string> HttpByteStreamProvider::Schemes() const {
  return {"http", "https"};
}

absl::StatusOr<std::unique_ptr<penumbra::ByteStream>>
HttpByteStreamProvider::Open(
    penumbra::ByteStreamRequest request,
    std::shared_ptr<penumbra::ByteStreamCallbacks> callbacks) {
  if (callbacks == nullptr) {
    return absl::InvalidArgumentError("callbacks are required");
  }
  const absl::StatusOr<UrlReference> parsed =
      UrlReference::ParseAbsolute(request.url);
  if (!parsed.ok() || !parsed->scheme.has_value() ||
      (*parsed->scheme != "http" && *parsed->scheme != "https") ||
      parsed->userinfo.has_value()) {
    return absl::InvalidArgumentError(
        "the URL must be an http or https URL without user information");
  }
  absl::StatusOr<std::unique_ptr<HttpByteStream>> stream =
      HttpByteStream::Start(context_, std::move(request), std::move(callbacks));
  if (!stream.ok()) {
    return stream.status();
  }
  return std::unique_ptr<penumbra::ByteStream>(*std::move(stream));
}

}
