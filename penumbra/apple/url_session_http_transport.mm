#include "penumbra/apple/url_session_http_transport.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/apple/url_session_metrics.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"

@interface AlumbraSessionDelegate : NSObject <NSURLSessionDataDelegate> {
 @public
  penumbra::apple::UrlSessionHttpTransport::Core* core_;
}
- (instancetype)initWithCore:(penumbra::apple::UrlSessionHttpTransport::Core*)core;
@end

namespace penumbra::apple {
namespace {

absl::Duration Now() {
  return absl::FromChrono(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()));
}

std::string String(NSString* value) {
  if (value == nil) {
    return {};
  }
  const char* utf8 = value.UTF8String;
  return utf8 == nullptr ? std::string() : std::string(utf8);
}

absl::Status ErrorFrom(NSError* error) {
  const std::string message = absl::StrCat("URLSession request failed (", error.code, ")");
  if (error.domain == NSURLErrorDomain && error.code == NSURLErrorTimedOut) {
    return absl::DeadlineExceededError(message);
  }
  if (error.domain == NSURLErrorDomain &&
      (error.code == NSURLErrorServerCertificateUntrusted ||
       error.code == NSURLErrorServerCertificateHasBadDate ||
       error.code == NSURLErrorServerCertificateHasUnknownRoot ||
       error.code == NSURLErrorServerCertificateNotYetValid ||
       error.code == NSURLErrorClientCertificateRejected ||
       error.code == NSURLErrorClientCertificateRequired ||
       error.code == NSURLErrorSecureConnectionFailed)) {
    return absl::UnauthenticatedError(absl::StrCat("TLS: ", message));
  }
  return absl::UnavailableError(message);
}

std::optional<absl::Duration> PhaseDuration(NSDate* start, NSDate* end) {
  if (start == nil || end == nil || [end compare:start] == NSOrderedAscending) {
    return std::nullopt;
  }
  return MapSessionPhase([start timeIntervalSinceReferenceDate],
                         [end timeIntervalSinceReferenceDate]);
}

struct Transfer {
  std::shared_ptr<HttpTransportCallbacks> callbacks;
  NSURLSessionDataTask* task = nil;
  std::atomic<bool> cancelled = false;
  std::atomic<bool> terminal = false;
  bool response_delivered = false;
  bool response_seen = false;
  int redirect_count = 0;
  int redirect_limit = 0;
  bool allow_downgrade = false;
  uint64_t body_bytes = 0;
  absl::Duration request_start = absl::ZeroDuration();
  absl::Duration response_start = absl::ZeroDuration();
  std::string requested_url;
  std::string final_url;
  HttpProtocol protocol = HttpProtocol::kUnknown;
  int status = 0;
  std::optional<absl::Status> redirect_error;
  std::optional<absl::Duration> dns;
  std::optional<absl::Duration> connect;
  std::optional<absl::Duration> tls;
};

}

class UrlSessionHttpTransport::Core
    : public std::enable_shared_from_this<UrlSessionHttpTransport::Core> {
 public:
  explicit Core(UrlSessionTransportOptions options) : options_(options) {}

  absl::Status Initialise() {
    @autoreleasepool {
      configuration_ = [NSURLSessionConfiguration ephemeralSessionConfiguration];
      configuration_.HTTPCookieStorage = nil;
      configuration_.URLCache = nil;
      configuration_.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
      configuration_.HTTPShouldSetCookies = NO;
      configuration_.timeoutIntervalForRequest =
          std::max(1.0, absl::ToDoubleSeconds(options_.read_timeout));
      if (@available(macOS 10.15, iOS 13.0, tvOS 13.0, *)) {
        configuration_.allowsConstrainedNetworkAccess = options_.allows_constrained_network_access;
        configuration_.allowsExpensiveNetworkAccess = options_.allows_expensive_network_access;
      }
      delegate_ = [[AlumbraSessionDelegate alloc] initWithCore:this];
      NSOperationQueue* const queue = [[NSOperationQueue alloc] init];
      queue.maxConcurrentOperationCount = 1;
      queue.name = @"com.alumbra.url-session-transport";
      session_ = [NSURLSession sessionWithConfiguration:configuration_
                                               delegate:delegate_
                                          delegateQueue:queue];
    }
    if (session_ == nil) {
      return absl::InternalError("URLSession initialisation failed");
    }
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<HttpRequestHandle>> Start(
      HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks);

  absl::Status Stop() {
    bool initiate = false;
    {
      const std::scoped_lock lock(mutex_);
      if (!stopped_) {
        stopped_ = true;
        initiate = true;
        for (auto& [identifier, transfer] : transfers_) {
          static_cast<void>(identifier);
          transfer->cancelled.store(true);
          [transfer->task cancel];
        }
      }
    }
    if (initiate) {
      [session_ invalidateAndCancel];
    }
    std::unique_lock lock(mutex_);
    if (!stopped_condition_.wait_for(lock, std::chrono::seconds(30),
                                     [this] { return invalidated_; })) {
      return absl::UnavailableError("URLSession stop barrier timed out");
    }
    transfers_.clear();
    return absl::OkStatus();
  }

  static void Cancel(const std::shared_ptr<Transfer>& transfer) {
    transfer->cancelled.store(true);
    [transfer->task cancel];
  }

  static void Pause(const std::shared_ptr<Transfer>& transfer) { [transfer->task suspend]; }

  static void Resume(const std::shared_ptr<Transfer>& transfer) { [transfer->task resume]; }

  void Response(NSURLSessionDataTask* task, NSURLResponse* response) {
    const auto transfer = Find(task.taskIdentifier);
    const auto http_response = (NSHTTPURLResponse*)response;
    if (transfer == nullptr || transfer->cancelled.load() || transfer->terminal.load() ||
        transfer->response_seen) {
      return;
    }
    transfer->response_seen = true;
    transfer->response_start = Now();
    transfer->status = static_cast<int>(http_response.statusCode);
    transfer->final_url = String(http_response.URL.absoluteString);
    transfer->protocol = HttpProtocol::kUnknown;
    HttpResponseHead head;
    head.status = transfer->status;
    head.protocol = transfer->protocol;
    head.final_url = transfer->final_url;
    NSDictionary* const fields = http_response.allHeaderFields;
    NSEnumerator* const enumerator = fields.keyEnumerator;
    id key = nil;
    while ((key = [enumerator nextObject]) != nil) {
      head.headers.push_back(
          {.name = String([key description]), .value = String([fields[key] description])});
    }
    transfer->response_delivered = true;
    if (!transfer->callbacks->OnResponse(std::move(head)).ok()) {
      Cancel(transfer);
    }
  }

  void Data(NSURLSessionDataTask* task, NSData* data) {
    const auto transfer = Find(task.taskIdentifier);
    if (transfer == nullptr || transfer->cancelled.load() || transfer->terminal.load() ||
        data.length == 0) {
      return;
    }
    const absl::Duration arrival_time = Now();
    if (!transfer->response_seen) {
      const auto response = (NSHTTPURLResponse*)task.response;
      Response(task, response);
      if (transfer->cancelled.load()) {
        return;
      }
    }
    const size_t size = data.length;
    const auto* bytes = static_cast<const uint8_t*>(data.bytes);
    std::shared_ptr<const void> owner((__bridge_retained const void*)data,
                                      [](const void* object) { CFRelease(object); });
    transfer->body_bytes += size;
    if (!transfer->callbacks
             ->OnChunk({.data = SharedBytes::Wrap(std::move(owner),
                                                  std::span<const uint8_t>(bytes, size)),
                        .arrival_time = arrival_time})
             .ok()) {
      Cancel(transfer);
    }
  }

  void Metrics(NSURLSessionTask* task, NSURLSessionTaskMetrics* metrics) {
    const auto transfer = Find(task.taskIdentifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      return;
    }
    NSEnumerator* const enumerator = metrics.transactionMetrics.objectEnumerator;
    NSURLSessionTaskTransactionMetrics* transaction = nil;
    while ((transaction = [enumerator nextObject]) != nil) {
      transfer->protocol = ProtocolFromName(String(transaction.networkProtocolName));
      transfer->dns =
          PhaseDuration(transaction.domainLookupStartDate, transaction.domainLookupEndDate);
      transfer->connect = PhaseDuration(transaction.connectStartDate, transaction.connectEndDate);
      transfer->tls =
          PhaseDuration(transaction.secureConnectionStartDate, transaction.secureConnectionEndDate);
    }
  }

  void Redirect(NSURLSessionTask* task, NSHTTPURLResponse* response, NSURLRequest* request,
                void (^completion)(NSURLRequest* _Nullable)) {
    const auto transfer = Find(task.taskIdentifier);
    if (transfer == nullptr || transfer->cancelled.load()) {
      completion(nil);
      return;
    }
    const std::string source = String(response.URL.absoluteString);
    const std::string destination = String(request.URL.absoluteString);
    const bool downgrade = source.starts_with("https:") && destination.starts_with("http:");
    ++transfer->redirect_count;
    if (transfer->redirect_count > transfer->redirect_limit ||
        (downgrade && !transfer->allow_downgrade)) {
      transfer->redirect_error = absl::FailedPreconditionError(
          downgrade ? "HTTPS to HTTP redirect refused" : "too many redirects");
      completion(nil);
      return;
    }
    completion(request);
  }

  void Complete(NSURLSessionTask* task, NSError* error) {
    const auto transfer = Find(task.taskIdentifier);
    if (transfer == nullptr || transfer->terminal.load()) {
      return;
    }
    if (transfer->cancelled.load()) {
      transfer->terminal.store(true);
      Erase(task.taskIdentifier);
      return;
    }
    if (transfer->redirect_error.has_value()) {
      transfer->terminal.store(true);
      static_cast<void>(transfer->callbacks->OnError(*transfer->redirect_error));
      Erase(task.taskIdentifier);
      return;
    }
    if (error != nil) {
      transfer->terminal.store(true);
      if (error.domain == NSURLErrorDomain && error.code == NSURLErrorCancelled) {
        transfer->cancelled.store(true);
        Erase(task.taskIdentifier);
        return;
      }
      static_cast<void>(transfer->callbacks->OnError(ErrorFrom(error)));
      Erase(task.taskIdentifier);
      return;
    }
    if (!transfer->response_seen && task.response != nil) {
      Response((NSURLSessionDataTask*)task, task.response);
    }
    if (transfer->cancelled.load() || transfer->terminal.exchange(true)) {
      Erase(task.taskIdentifier);
      return;
    }
    HttpMetrics result;
    result.status = transfer->status;
    result.protocol = transfer->protocol;
    result.final_url = transfer->final_url;
    result.request_start = transfer->request_start;
    result.response_start = transfer->response_start;
    result.completion = Now();
    result.body_bytes = transfer->body_bytes;
    result.dns = transfer->dns;
    result.connect = transfer->connect;
    result.tls = transfer->tls;
    static_cast<void>(transfer->callbacks->OnComplete(std::move(result)));
    Erase(task.taskIdentifier);
  }

  void Invalidated() {
    const std::scoped_lock lock(mutex_);
    invalidated_ = true;
    stopped_condition_.notify_all();
  }

  std::shared_ptr<Transfer> Find(NSUInteger identifier) {
    const std::scoped_lock lock(mutex_);
    const auto found = transfers_.find(identifier);
    return found == transfers_.end() ? nullptr : found->second;
  }

  void Erase(NSUInteger identifier) {
    const std::scoped_lock lock(mutex_);
    transfers_.erase(identifier);
  }

  UrlSessionTransportOptions options_;
  NSURLSessionConfiguration* configuration_ = nil;
  NSURLSession* session_ = nil;
  id delegate_ = nil;
  std::mutex mutex_;
  std::condition_variable stopped_condition_;
  std::map<NSUInteger, std::shared_ptr<Transfer>> transfers_;
  bool stopped_ = false;
  bool invalidated_ = false;
};

HttpProtocol ProtocolFromName(std::string_view name) {
  if (name == "http/1.0") {
    return HttpProtocol::kHttp1_0;
  }
  if (name == "http/1.1") {
    return HttpProtocol::kHttp1_1;
  }
  if (name == "h2") {
    return HttpProtocol::kHttp2;
  }
  if (name == "h3") {
    return HttpProtocol::kHttp3;
  }
  return HttpProtocol::kUnknown;
}

std::optional<absl::Duration> MapSessionPhase(std::optional<double> start,
                                              std::optional<double> end) {
  if (!start.has_value() || !end.has_value() || *end < *start) {
    return std::nullopt;
  }
  return absl::Seconds(*end - *start);
}

}

@implementation AlumbraSessionDelegate
- (instancetype)initWithCore:(penumbra::apple::UrlSessionHttpTransport::Core*)core {
  self = [super init];
  if (self != nil) {
    core_ = core;
  }
  return self;
}
- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
  static_cast<void>(session);
  core_->Response(dataTask, response);
  completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveData:(NSData*)data {
  static_cast<void>(session);
  core_->Data(dataTask, data);
}
- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
  static_cast<void>(session);
  core_->Complete(task, error);
}
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    didFinishCollectingMetrics:(NSURLSessionTaskMetrics*)metrics {
  static_cast<void>(session);
  core_->Metrics(task, metrics);
}
- (void)URLSession:(NSURLSession*)session didBecomeInvalidWithError:(NSError*)error {
  static_cast<void>(session);
  static_cast<void>(error);
  core_->Invalidated();
}
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
  static_cast<void>(session);
  core_->Redirect(task, response, request, completionHandler);
}
@end

namespace penumbra::apple {
namespace {

class UrlSessionRequestHandle final : public HttpRequestHandle {
 public:
  UrlSessionRequestHandle(std::shared_ptr<UrlSessionHttpTransport::Core> core,
                          std::shared_ptr<Transfer> transfer)
      : core_(std::move(core)), transfer_(std::move(transfer)) {}
  UrlSessionRequestHandle(const UrlSessionRequestHandle&) = delete;
  UrlSessionRequestHandle& operator=(const UrlSessionRequestHandle&) = delete;
  ~UrlSessionRequestHandle() override { Cancel(); }
  void Cancel() override { UrlSessionHttpTransport::Core::Cancel(transfer_); }
  absl::Status Pause() override {
    if (transfer_->cancelled.load() || transfer_->terminal) {
      return absl::FailedPreconditionError("request is no longer active");
    }
    UrlSessionHttpTransport::Core::Pause(transfer_);
    return absl::OkStatus();
  }
  absl::Status Resume() override {
    if (transfer_->cancelled.load() || transfer_->terminal) {
      return absl::FailedPreconditionError("request is no longer active");
    }
    UrlSessionHttpTransport::Core::Resume(transfer_);
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<UrlSessionHttpTransport::Core> core_;
  std::shared_ptr<Transfer> transfer_;
};

}

absl::StatusOr<std::unique_ptr<HttpRequestHandle>> UrlSessionHttpTransport::Core::Start(
    HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
  if (request.url.empty() || callbacks == nullptr ||
      request.connect_timeout <= absl::ZeroDuration() || request.redirects.max_redirects < 0 ||
      (request.method != HttpMethod::kGet && request.method != HttpMethod::kHead &&
       request.method != HttpMethod::kPost) ||
      (request.method != HttpMethod::kPost && !request.body.empty()) ||
      (request.range.has_value() && request.range->length == 0) ||
      (request.range.has_value() && request.range->length.has_value() &&
       *request.range->length - 1 > std::numeric_limits<uint64_t>::max() - request.range->offset)) {
    return absl::InvalidArgumentError("invalid HTTP request");
  }
  NSString* const url_string = [NSString stringWithUTF8String:request.url.c_str()];
  if (url_string == nil) {
    return absl::InvalidArgumentError("invalid HTTP URL");
  }
  NSURL* const url = [NSURL URLWithString:url_string];
  if (url == nil || (![url.scheme.lowercaseString isEqualToString:@"http"] &&
                     ![url.scheme.lowercaseString isEqualToString:@"https"])) {
    return absl::InvalidArgumentError("invalid HTTP URL");
  }
  auto transfer = std::make_shared<Transfer>();
  transfer->callbacks = std::move(callbacks);
  transfer->request_start = Now();
  transfer->requested_url = request.url;
  transfer->redirect_limit = request.redirects.max_redirects;
  transfer->allow_downgrade = request.redirects.allow_https_downgrade;
  NSMutableURLRequest* const native_request =
      [NSMutableURLRequest requestWithURL:url
                              cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                          timeoutInterval:absl::ToDoubleSeconds(request.connect_timeout)];
  switch (request.method) {
    case HttpMethod::kGet:
      native_request.HTTPMethod = @"GET";
      break;
    case HttpMethod::kHead:
      native_request.HTTPMethod = @"HEAD";
      break;
    case HttpMethod::kPost:
      native_request.HTTPMethod = @"POST";
      break;
  }
  if (@available(macOS 10.15, iOS 13.0, tvOS 13.0, *)) {
    native_request.allowsConstrainedNetworkAccess = options_.allows_constrained_network_access;
    native_request.allowsExpensiveNetworkAccess = options_.allows_expensive_network_access;
  }
  if (@available(macOS 11.3, iOS 14.5, tvOS 14.5, *)) {
    native_request.assumesHTTP3Capable = options_.assume_http3_capable;
  }
  for (const HttpHeader& header : request.headers) {
    NSString* const name = [NSString stringWithUTF8String:header.name.c_str()];
    NSString* const value = [NSString stringWithUTF8String:header.value.c_str()];
    if (name == nil || value == nil) {
      return absl::InvalidArgumentError("invalid HTTP header");
    }
    [native_request setValue:value forHTTPHeaderField:name];
  }
  if (request.range.has_value()) {
    const ByteRange range = *request.range;
    std::string value = absl::StrCat("bytes=", range.offset, "-");
    if (range.length.has_value() && *range.length > 0) {
      value = absl::StrCat("bytes=", range.offset, "-", range.offset + *range.length - 1);
    }
    [native_request setValue:[NSString stringWithUTF8String:value.c_str()]
          forHTTPHeaderField:@"Range"];
  }
  if (!request.body.empty()) {
    native_request.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];
  }
  {
    const std::scoped_lock lock(mutex_);
    if (stopped_) {
      return absl::FailedPreconditionError("transport is stopped");
    }
    transfer->task = [session_ dataTaskWithRequest:native_request];
    transfers_.emplace(transfer->task.taskIdentifier, transfer);
  }
  [transfer->task resume];
  return std::unique_ptr<HttpRequestHandle>(
      new UrlSessionRequestHandle(shared_from_this(), std::move(transfer)));
}

absl::StatusOr<std::shared_ptr<UrlSessionHttpTransport>> UrlSessionHttpTransport::Create(
    UrlSessionTransportOptions options) {
  if (options.read_timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError("read timeout must be positive");
  }
  auto core = std::make_shared<Core>(options);
  absl::Status status = core->Initialise();
  if (!status.ok()) {
    return status;
  }
  return std::shared_ptr<UrlSessionHttpTransport>(new UrlSessionHttpTransport(std::move(core)));
}

UrlSessionHttpTransport::UrlSessionHttpTransport(std::shared_ptr<Core> core)
    : core_(std::move(core)) {}

UrlSessionHttpTransport::~UrlSessionHttpTransport() { static_cast<void>(core_->Stop()); }

absl::StatusOr<std::unique_ptr<HttpRequestHandle>> UrlSessionHttpTransport::Start(
    HttpRequest request, std::shared_ptr<HttpTransportCallbacks> callbacks) {
  return core_->Start(std::move(request), std::move(callbacks));
}

absl::Status UrlSessionHttpTransport::Stop() { return core_->Stop(); }

}
