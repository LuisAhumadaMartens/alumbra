#include "umbra/manifest/dash_live.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "umbra/base/task_runner.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/manifest_error.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/refresh.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::manifest {
namespace {

constexpr std::string_view kXsDate = "urn:mpeg:dash:utc:http-xsdate:2014";
constexpr std::string_view kIso = "urn:mpeg:dash:utc:http-iso:2014";
constexpr std::string_view kHead = "urn:mpeg:dash:utc:http-head:2014";
constexpr std::string_view kNtp = "urn:mpeg:dash:utc:http-ntp:2014";
constexpr std::string_view kDirect = "urn:mpeg:dash:utc:direct:2014";
constexpr std::string_view kRfc3339Full = "%Y-%m-%dT%H:%M:%E*S%Ez";
constexpr std::int64_t kNtpUnixEpoch = 2'208'988'800;

std::optional<absl::Time> ParseTime(std::string_view text) {
  absl::Time result;
  std::string error;
  if (absl::ParseTime(kRfc3339Full, text, &result, &error)) {
    return result;
  }
  return std::nullopt;
}

std::optional<absl::Time> ParseHttpDate(std::string_view text) {
  absl::Time result;
  std::string error;
  constexpr std::string_view kHttpDateFormat = "%a, %d %b %Y %H:%M:%S GMT";
  if (absl::ParseTime(kHttpDateFormat, text, &result, &error)) {
    return result;
  }
  return std::nullopt;
}

std::optional<absl::Time> ParseNtp(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 48) {
    return std::nullopt;
  }
  std::uint32_t seconds = 0;
  std::uint32_t fraction = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    seconds = (seconds << 8) | bytes[40 + i];
    fraction = (fraction << 8) | bytes[44 + i];
  }
  if (seconds < kNtpUnixEpoch) {
    return std::nullopt;
  }
  const std::int64_t unix_seconds =
      static_cast<std::int64_t>(seconds) - kNtpUnixEpoch;
  const std::int64_t micros =
      static_cast<std::int64_t>(fraction) * 1'000'000 / 4'294'967'296LL;
  return absl::UnixEpoch() + absl::Seconds(unix_seconds) +
         absl::Microseconds(micros);
}

std::string HeaderValue(const std::vector<penumbra::HttpHeader>& headers,
                        std::string_view name) {
  for (const penumbra::HttpHeader& header : headers) {
    if (header.name.size() != name.size()) {
      continue;
    }
    bool equal = true;
    for (std::size_t i = 0; i < name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(header.name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        equal = false;
        break;
      }
    }
    if (equal) {
      return header.value;
    }
  }
  return {};
}

std::string NormalizeUriPath(std::string value) {
  const std::size_t scheme = value.find("://");
  if (scheme == std::string::npos) {
    return value;
  }
  const std::size_t authority_start = scheme + 3;
  const std::size_t path_start = value.find('/', authority_start);
  if (path_start == std::string::npos) {
    return value;
  }
  const std::size_t suffix_start = value.find_first_of("?#", path_start);
  const std::string prefix = value.substr(0, path_start);
  const std::string path =
      value.substr(path_start, suffix_start == std::string::npos
                                   ? std::string::npos
                                   : suffix_start - path_start);
  const std::string suffix = suffix_start == std::string::npos
                                 ? std::string()
                                 : value.substr(suffix_start);
  std::vector<std::string_view> parts;
  std::size_t begin = 1;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::string_view part = std::string_view(path).substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (part == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  std::string normalised = prefix + "/";
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      normalised.push_back('/');
    }
    normalised.append(parts[i]);
  }
  if (path.ends_with('/') || path.ends_with("/.") || path.ends_with("/..")) {
    normalised.push_back('/');
  }
  normalised.append(suffix);
  return normalised;
}

std::string ResolveLocation(std::string_view base, std::string_view reference) {
  if (reference.find("://") != std::string_view::npos) {
    return std::string(reference);
  }
  const std::size_t scheme = base.find("://");
  if (scheme == std::string_view::npos) {
    return std::string(reference);
  }
  if (reference.starts_with("//")) {
    return absl::StrCat(base.substr(0, scheme + 1), reference);
  }
  const std::size_t authority_start = scheme + 3;
  const std::size_t path_start = base.find('/', authority_start);
  const std::string_view origin =
      path_start == std::string_view::npos ? base : base.substr(0, path_start);
  if (reference.starts_with('/')) {
    return NormalizeUriPath(absl::StrCat(origin, reference));
  }
  if (reference.starts_with('?') || reference.starts_with('#')) {
    const std::size_t suffix = base.find_first_of("?#");
    return absl::StrCat(
        suffix == std::string_view::npos ? base : base.substr(0, suffix),
        reference);
  }
  if (path_start == std::string_view::npos) {
    return NormalizeUriPath(absl::StrCat(origin, "/", reference));
  }
  const std::size_t base_suffix = base.find_first_of("?#");
  const std::string_view clean_base = base_suffix == std::string_view::npos
                                          ? base
                                          : base.substr(0, base_suffix);
  const std::size_t slash = clean_base.find_last_of('/');
  const std::string_view directory = slash == std::string_view::npos
                                         ? clean_base
                                         : clean_base.substr(0, slash + 1);
  return NormalizeUriPath(absl::StrCat(directory, reference));
}

struct TimingEntry {
  std::string scheme;
  std::string value;
};

class TimingOperation;

class TimingReceiver final : public penumbra::HttpTransportCallbacks {
 public:
  TimingReceiver(std::shared_ptr<TimingOperation> operation,
                 std::uint64_t generation)
      : operation_(std::move(operation)), generation_(generation) {}

  void Clear() {
    const std::scoped_lock lock(mutex_);
    operation_.reset();
  }

  absl::Status OnResponse(penumbra::HttpResponseHead head) override;
  absl::Status OnChunk(penumbra::HttpChunk chunk) override;
  absl::Status OnComplete(penumbra::HttpMetrics metrics) override;
  absl::Status OnError(absl::Status error) override;

 private:
  std::shared_ptr<TimingOperation> GetOperation() const {
    const std::scoped_lock lock(mutex_);
    return operation_;
  }

  mutable std::mutex mutex_;
  std::shared_ptr<TimingOperation> operation_;
  std::uint64_t generation_ = 0;
};

class TimingOperation final
    : public std::enable_shared_from_this<TimingOperation> {
 public:
  TimingOperation(std::vector<TimingEntry> entries,
                  std::shared_ptr<penumbra::HttpTransport> transport,
                  std::shared_ptr<base::SequencedTaskRunner> runner,
                  std::function<absl::Time()> wall_clock,
                  DashUtcTimingCallback callback)
      : entries_(std::move(entries)),
        transport_(std::move(transport)),
        runner_(std::move(runner)),
        wall_clock_(std::move(wall_clock)),
        callback_(std::move(callback)) {}

  void Start() {
    if (!runner_->RunsTasksInCurrentSequence()) {
      Finish(absl::FailedPreconditionError(
          "DASH UTC timing must start on its sequence"));
      return;
    }
    TryNext();
  }

  void SetReceiver(std::shared_ptr<TimingReceiver> receiver) {
    receiver_ = std::move(receiver);
  }

  void Reject() {
    Finish(absl::UnavailableError("DASH UTC timing sequence is stopped"));
  }

  void Response(std::uint64_t generation,
                const penumbra::HttpResponseHead& head) {
    if (done_ || generation != attempt_generation_) {
      return;
    }
    if (head.status < 200 || head.status >= 300) {
      TryNext();
      return;
    }
    response_seen_ = true;
    if (method_ == penumbra::HttpMethod::kHead) {
      const std::optional<absl::Time> server =
          ParseHttpDate(HeaderValue(head.headers, "date"));
      if (server.has_value()) {
        Finish(*server - request_started_);
      } else {
        TryNext();
      }
    }
  }

  void Chunk(std::uint64_t generation, const penumbra::HttpChunk& chunk) {
    if (done_ || generation != attempt_generation_ || !response_seen_ ||
        method_ == penumbra::HttpMethod::kHead || body_.size() > 4096 ||
        chunk.data.size() > 4096 - body_.size()) {
      return;
    }
    body_.insert(body_.end(), chunk.data.span().begin(),
                 chunk.data.span().end());
  }

  void Complete(std::uint64_t generation) {
    if (done_ || generation != attempt_generation_) {
      return;
    }
    std::optional<absl::Time> server;
    const std::string body(body_.begin(), body_.end());
    if (scheme_ == kNtp) {
      server = ParseNtp(body_);
    } else {
      server = ParseTime(body);
    }
    if (server.has_value()) {
      Finish(*server - request_started_);
    } else {
      TryNext();
    }
  }

  void Error(std::uint64_t generation) {
    if (!done_ && generation == attempt_generation_) {
      TryNext();
    }
  }

  bool Post(std::function<void()> task) {
    if (runner_->RunsTasksInCurrentSequence()) {
      task();
      return true;
    }
    return runner_->PostTask(std::move(task));
  }

 private:
  void TryNext() {
    handle_.reset();
    if (receiver_ != nullptr) {
      receiver_->Clear();
      receiver_.reset();
    }
    ++attempt_generation_;
    body_.clear();
    response_seen_ = false;
    if (entry_index_ >= entries_.size()) {
      Finish(absl::UnavailableError("all DASH UTC timing sources failed"));
      return;
    }
    const TimingEntry& entry = entries_[entry_index_++];
    scheme_ = entry.scheme;
    if (scheme_ == kDirect) {
      const std::optional<absl::Time> server = ParseTime(entry.value);
      if (server.has_value()) {
        Finish(*server - wall_clock_());
      } else {
        TryNext();
      }
      return;
    }
    if (scheme_ != kXsDate && scheme_ != kIso && scheme_ != kHead &&
        scheme_ != kNtp) {
      TryNext();
      return;
    }
    penumbra::HttpRequest request;
    method_ = scheme_ == kHead ? penumbra::HttpMethod::kHead
                               : penumbra::HttpMethod::kGet;
    request.method = method_;
    request.url = entry.value;
    request.request_class = penumbra::RequestClass::kManifest;
    request.priority = penumbra::RequestPriority::kHigh;
    request_started_ = wall_clock_();
    auto receiver = std::make_shared<TimingReceiver>(shared_from_this(),
                                                     attempt_generation_);
    SetReceiver(receiver);
    auto started = transport_->Start(std::move(request), receiver);
    if (!started.ok()) {
      TryNext();
      return;
    }
    handle_ = std::move(*started);
  }

  void Finish(absl::StatusOr<absl::Duration> result) {
    if (done_) {
      return;
    }
    done_ = true;
    handle_.reset();
    if (receiver_ != nullptr) {
      receiver_->Clear();
      receiver_.reset();
    }
    const DashUtcTimingCallback callback = std::move(callback_);
    callback(std::move(result));
  }

  std::vector<TimingEntry> entries_;
  std::shared_ptr<penumbra::HttpTransport> transport_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::function<absl::Time()> wall_clock_;
  DashUtcTimingCallback callback_;
  std::unique_ptr<penumbra::HttpRequestHandle> handle_;
  std::shared_ptr<TimingReceiver> receiver_;
  std::vector<std::uint8_t> body_;
  std::size_t entry_index_ = 0;
  std::string scheme_;
  penumbra::HttpMethod method_ = penumbra::HttpMethod::kGet;
  absl::Time request_started_;
  bool response_seen_ = false;
  bool done_ = false;
  std::uint64_t attempt_generation_ = 0;
};

std::vector<const Track*> ResolvedTracks(const Presentation& presentation);
void AppendResolvedTracks(const Period& period,
                          std::vector<const Track*>& tracks);

penumbra::MediaTime TrackAvailableEdge(const Track& track,
                                       absl::Duration elapsed);

absl::StatusOr<DashAvailabilityWindow> ComputeWindow(
    const Presentation& presentation, absl::Time utc_now,
    absl::Duration utc_offset) {
  if (presentation.protocol != Protocol::kDash ||
      presentation.type != PresentationType::kDynamic ||
      !presentation.live.has_value() ||
      !presentation.live->availability_start_time.has_value()) {
    return InvalidManifest(
        "DASH availability requires a dynamic MPD with availabilityStartTime");
  }
  const LiveInfo& live = *presentation.live;
  const absl::Duration elapsed =
      utc_now + utc_offset - *live.availability_start_time;
  penumbra::MediaTime available_end = penumbra::MediaTime::FromMicroseconds(
      std::numeric_limits<std::int64_t>::max());
  penumbra::MediaTime earliest = penumbra::MediaTime::FromMicroseconds(
      std::numeric_limits<std::int64_t>::max());
  const std::vector<const Track*> tracks = ResolvedTracks(presentation);
  for (const Track* track : tracks) {
    earliest = std::min(earliest, track->segments.Index()->Start());
    available_end =
        std::min(available_end, TrackAvailableEdge(*track, elapsed));
  }
  if (tracks.empty()) {
    available_end = penumbra::MediaTime::Zero();
    earliest = available_end;
  }
  penumbra::MediaTime start = earliest;
  if (live.time_shift_depth.has_value() && live.time_shift_depth->is_finite()) {
    start = std::max(start, available_end - *live.time_shift_depth);
  }
  return DashAvailabilityWindow{
      .start = start, .end = available_end, .available_end = available_end};
}

std::vector<const Track*> ResolvedTracks(const Presentation& presentation) {
  std::vector<const Track*> tracks;
  for (const auto& period : presentation.periods) {
    if (period != nullptr) {
      AppendResolvedTracks(*period, tracks);
    }
  }
  return tracks;
}

void AppendResolvedTracks(const Period& period,
                          std::vector<const Track*>& tracks) {
  for (const auto& set : period.switching_sets) {
    if (set == nullptr) {
      continue;
    }
    for (const auto& track : set->tracks) {
      if (track == nullptr || !track->segments.is_resolved() ||
          track->segments.Index() == nullptr ||
          track->segments.Index()->empty()) {
        continue;
      }
      tracks.push_back(track.get());
    }
  }
}

penumbra::MediaTime TrackAvailableEdge(const Track& track,
                                       absl::Duration elapsed) {
  const SegmentIndex& index = *track.segments.Index();
  const auto* extension = std::get_if<DashTrackExtension>(&track.extension);
  if (extension != nullptr && extension->availability_time_offset_infinite) {
    return index.End();
  }
  const absl::Duration availability_offset =
      extension != nullptr && extension->availability_time_offset.has_value()
          ? absl::Microseconds(
                extension->availability_time_offset->InMicroseconds())
          : absl::ZeroDuration();
  const penumbra::MediaTime allowed =
      penumbra::MediaTime::FromMicroseconds(std::max<std::int64_t>(
          0, absl::ToInt64Microseconds(elapsed + availability_offset)));
  std::size_t low = 0;
  std::size_t high = index.size();
  while (low < high) {
    const std::size_t middle = low + ((high - low) / 2);
    if (index.EndOf(middle) <= allowed) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low == 0 ? penumbra::MediaTime::Zero() : index.EndOf(low - 1);
}

}

absl::Status TimingReceiver::OnResponse(penumbra::HttpResponseHead head) {
  if (const auto operation = GetOperation()) {
    const std::uint64_t generation = generation_;
    operation->Post([operation, generation, head = std::move(head)] {
      operation->Response(generation, head);
    });
  }
  return absl::OkStatus();
}

absl::Status TimingReceiver::OnChunk(penumbra::HttpChunk chunk) {
  if (const auto operation = GetOperation()) {
    const std::uint64_t generation = generation_;
    operation->Post([operation, generation, chunk = std::move(chunk)] {
      operation->Chunk(generation, chunk);
    });
  }
  return absl::OkStatus();
}

absl::Status TimingReceiver::OnComplete(penumbra::HttpMetrics metrics) {
  static_cast<void>(metrics);
  if (const auto operation = GetOperation()) {
    const std::uint64_t generation = generation_;
    operation->Post(
        [operation, generation] { operation->Complete(generation); });
  }
  return absl::OkStatus();
}

absl::Status TimingReceiver::OnError(absl::Status error) {
  static_cast<void>(error);
  if (const auto operation = GetOperation()) {
    const std::uint64_t generation = generation_;
    operation->Post([operation, generation] { operation->Error(generation); });
  }
  return absl::OkStatus();
}

void ResolveDashUtcOffset(const Presentation& presentation,
                          std::shared_ptr<penumbra::HttpTransport> transport,
                          std::shared_ptr<base::SequencedTaskRunner> runner,
                          std::function<absl::Time()> wall_clock,
                          std::string fallback_url,
                          DashUtcTimingCallback callback) {
  std::vector<TimingEntry> entries;
  if (presentation.dash_extension.has_value()) {
    for (const DashUtcTiming& timing :
         presentation.dash_extension->utc_timings) {
      entries.push_back({timing.scheme_id_uri, timing.value});
    }
  }
  if (entries.empty() && !fallback_url.empty()) {
    entries.push_back({std::string(kXsDate), std::move(fallback_url)});
  }
  if (entries.empty()) {
    callback(absl::ZeroDuration());
    return;
  }
  if (transport == nullptr || runner == nullptr || !wall_clock) {
    callback(
        absl::InvalidArgumentError("DASH UTC timing dependencies are missing"));
    return;
  }
  auto operation = std::make_shared<TimingOperation>(
      std::move(entries), std::move(transport), std::move(runner),
      std::move(wall_clock), std::move(callback));
  const auto start = [operation] { operation->Start(); };
  if (operation->Post(start)) {
    return;
  }
  operation->Reject();
}

absl::StatusOr<DashAvailabilityWindow> DashAvailableWindow(
    const Presentation& presentation, absl::Time utc_now,
    absl::Duration utc_offset) {
  return ComputeWindow(presentation, utc_now, utc_offset);
}

absl::StatusOr<penumbra::MediaTime> DashLiveStartPosition(
    const Presentation& presentation, absl::Time utc_now,
    absl::Duration utc_offset, std::int64_t start_delay_ms) {
  if (start_delay_ms < -1 || start_delay_ms > 3'600'000) {
    return InvalidManifest("DASH live start delay is out of range");
  }
  auto window = ComputeWindow(presentation, utc_now, utc_offset);
  if (!window.ok()) {
    return window.status();
  }
  if (!presentation.live.has_value()) {
    return InvalidManifest("DASH live start requires live presentation data");
  }
  const LiveInfo& live = *presentation.live;
  absl::Duration delay;
  if (start_delay_ms >= 0) {
    delay = absl::Milliseconds(start_delay_ms);
  } else {
    const auto suggested_delay = live.suggested_presentation_delay;
    if (suggested_delay.has_value()) {
      delay = absl::Microseconds(suggested_delay->InMicroseconds());
    } else {
      const auto target_duration = live.target_segment_duration;
      if (!target_duration.has_value()) {
        return InvalidManifest(
            "DASH live MPD has no start delay or target duration");
      }
      delay = absl::Microseconds(target_duration->InMicroseconds() * 3);
    }
  }
  const penumbra::MediaTime position =
      window->available_end -
      penumbra::TimeDelta::FromMicroseconds(absl::ToInt64Microseconds(delay));
  return std::clamp(position, window->start, window->available_end);
}

DashRefreshPlan PlanDashRefresh(const Presentation& presentation,
                                std::string_view manifest_url) {
  DashRefreshPlan plan;
  if (presentation.protocol != Protocol::kDash) {
    return plan;
  }
  if (presentation.type == PresentationType::kStatic) {
    plan.end_playback = true;
    return plan;
  }
  if (!presentation.live.has_value() ||
      !presentation.live->minimum_update_period.has_value()) {
    return plan;
  }
  const std::int64_t delay_us =
      presentation.live->minimum_update_period->InMicroseconds();
  if (delay_us <= 0) {
    return plan;
  }
  plan.refresh = true;
  plan.delay = absl::Microseconds(delay_us);
  plan.manifest_url = std::string(manifest_url);
  if (presentation.dash_extension.has_value() &&
      !presentation.dash_extension->locations.empty()) {
    const std::string& location =
        presentation.dash_extension->locations.front();
    plan.manifest_url = ResolveLocation(manifest_url, location);
  }
  return plan;
}

class DashMpdRefresher::Impl final
    : public std::enable_shared_from_this<DashMpdRefresher::Impl> {
 public:
  class Receiver final : public penumbra::HttpTransportCallbacks {
   public:
    Receiver(std::weak_ptr<Impl> owner, std::uint64_t generation)
        : owner_(std::move(owner)), generation_(generation) {}

    absl::Status OnResponse(penumbra::HttpResponseHead head) override {
      if (const std::shared_ptr<Impl> owner = owner_.lock()) {
        const std::uint64_t generation = generation_;
        owner->Post([owner, generation, head = std::move(head)] {
          owner->Response(generation, head);
        });
      }
      return absl::OkStatus();
    }

    absl::Status OnChunk(penumbra::HttpChunk chunk) override {
      if (const std::shared_ptr<Impl> owner = owner_.lock()) {
        const std::uint64_t generation = generation_;
        owner->Post([owner, generation, chunk = std::move(chunk)] {
          owner->Chunk(generation, chunk);
        });
      }
      return absl::OkStatus();
    }

    absl::Status OnComplete(penumbra::HttpMetrics metrics) override {
      if (const std::shared_ptr<Impl> owner = owner_.lock()) {
        const std::uint64_t generation = generation_;
        owner->Post(
            [owner, generation, metrics = std::move(metrics)]() mutable {
              owner->Complete(generation, std::move(metrics));
            });
      }
      return absl::OkStatus();
    }

    absl::Status OnError(absl::Status error) override {
      if (const std::shared_ptr<Impl> owner = owner_.lock()) {
        const std::uint64_t generation = generation_;
        owner->Post([owner, generation, error = std::move(error)]() mutable {
          owner->Error(generation, std::move(error));
        });
      }
      return absl::OkStatus();
    }

   private:
    std::weak_ptr<Impl> owner_;
    std::uint64_t generation_ = 0;
  };

  explicit Impl(DashMpdRefresherConfig config)
      : config_(std::move(config)),
        presentation_(config_.presentation),
        manifest_url_(config_.manifest_url) {}

  void Start() {
    if (stopped_ || started_) {
      return;
    }
    started_ = true;
    Schedule();
  }

  void Stop() {
    stopped_ = true;
    ++generation_;
    handle_.reset();
  }

  bool Post(std::function<void()> task) const {
    if (config_.runner->RunsTasksInCurrentSequence()) {
      task();
      return true;
    }
    return config_.runner->PostTask(std::move(task));
  }

 private:
  void Schedule() {
    if (stopped_) {
      return;
    }
    const DashRefreshPlan plan = PlanDashRefresh(*presentation_, manifest_url_);
    if (!plan.refresh) {
      if (plan.end_playback) {
        config_.on_update(DashMpdRefreshUpdate{.presentation = presentation_,
                                               .manifest_url = manifest_url_,
                                               .ended = true});
      }
      return;
    }
    const std::weak_ptr<Impl> weak = weak_from_this();
    config_.runner->PostDelayedTask(
        [weak, url = plan.manifest_url] {
          if (const std::shared_ptr<Impl> owner = weak.lock()) {
            owner->Request(url);
          }
        },
        plan.delay);
  }

  void Request(std::string url) {
    if (stopped_) {
      return;
    }
    handle_.reset();
    body_.clear();
    response_url_.clear();
    requested_url_ = std::move(url);
    const std::uint64_t request_generation = ++generation_;
    penumbra::HttpRequest request;
    request.method = penumbra::HttpMethod::kGet;
    request.url = requested_url_;
    request.request_class = penumbra::RequestClass::kManifest;
    request.priority = penumbra::RequestPriority::kHigh;
    auto receiver =
        std::make_shared<Receiver>(weak_from_this(), request_generation);
    auto started = config_.transport->Start(std::move(request), receiver);
    if (!started.ok()) {
      Notify(started.status());
      ScheduleRetry();
      return;
    }
    handle_ = std::move(*started);
  }

  void Response(std::uint64_t generation, penumbra::HttpResponseHead head) {
    if (stopped_ || generation != generation_) {
      return;
    }
    if (head.status < 200 || head.status >= 300) {
      Notify(absl::UnavailableError(
          "DASH MPD refresh returned a non-success status"));
      ScheduleRetry();
      return;
    }
    response_url_ = std::move(head.final_url);
  }

  void Chunk(std::uint64_t generation, const penumbra::HttpChunk& chunk) {
    if (stopped_ || generation != generation_) {
      return;
    }
    if (chunk.data.size() >
        config_.max_mpd_bytes - std::min(config_.max_mpd_bytes, body_.size())) {
      Notify(InvalidManifest("refreshed MPD exceeds configured byte limit"));
      ScheduleRetry();
      return;
    }
    body_.insert(body_.end(), chunk.data.span().begin(),
                 chunk.data.span().end());
  }

  void Complete(std::uint64_t generation, penumbra::HttpMetrics metrics) {
    if (stopped_ || generation != generation_) {
      return;
    }
    handle_.reset();
    const std::string xml(body_.begin(), body_.end());
    auto parsed = ParseDashMpd(xml, config_.max_mpd_bytes);
    if (!parsed.ok()) {
      Notify(parsed.status());
      ScheduleRetry();
      return;
    }
    auto refreshed = RefreshPresentation(presentation_, std::move(*parsed));
    if (!refreshed.ok()) {
      Notify(refreshed.status());
      ScheduleRetry();
      return;
    }
    presentation_ = std::move(*refreshed);
    if (!response_url_.empty()) {
      manifest_url_ = std::move(response_url_);
    } else if (!metrics.final_url.empty()) {
      manifest_url_ = std::move(metrics.final_url);
    }
    config_.on_update(DashMpdRefreshUpdate{
        .presentation = presentation_,
        .manifest_url = manifest_url_,
        .ended = presentation_->type == PresentationType::kStatic});
    Schedule();
  }

  void Error(std::uint64_t generation, absl::Status error) {
    if (stopped_ || generation != generation_) {
      return;
    }
    handle_.reset();
    Notify(std::move(error));
    ScheduleRetry();
  }

  void Notify(absl::Status status) const {
    config_.on_update(std::move(status));
  }

  void ScheduleRetry() {
    handle_.reset();
    body_.clear();
    ++generation_;
    if (stopped_) {
      return;
    }
    const absl::Duration delay =
        presentation_->live.has_value() &&
                presentation_->live->minimum_update_period.has_value()
            ? std::max(
                  absl::Seconds(1),
                  absl::Microseconds(presentation_->live->minimum_update_period
                                         ->InMicroseconds()))
            : absl::Seconds(1);
    const std::weak_ptr<Impl> weak = weak_from_this();
    config_.runner->PostDelayedTask(
        [weak] {
          if (const std::shared_ptr<Impl> owner = weak.lock()) {
            owner->Schedule();
          }
        },
        delay);
  }

  DashMpdRefresherConfig config_;
  std::shared_ptr<const Presentation> presentation_;
  std::string manifest_url_;
  std::string requested_url_;
  std::string response_url_;
  std::vector<std::uint8_t> body_;
  std::unique_ptr<penumbra::HttpRequestHandle> handle_;
  std::uint64_t generation_ = 0;
  bool started_ = false;
  bool stopped_ = false;
};

absl::StatusOr<std::unique_ptr<DashMpdRefresher>> DashMpdRefresher::Create(
    DashMpdRefresherConfig config) {
  if (config.presentation == nullptr ||
      config.presentation->protocol != Protocol::kDash ||
      config.transport == nullptr || config.runner == nullptr ||
      config.on_update == nullptr || config.max_mpd_bytes == 0) {
    return absl::InvalidArgumentError(
        "invalid DASH MPD refresher configuration");
  }
  auto impl = std::make_shared<Impl>(std::move(config));
  return std::unique_ptr<DashMpdRefresher>(
      new DashMpdRefresher(std::move(impl)));
}

DashMpdRefresher::DashMpdRefresher(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

DashMpdRefresher::~DashMpdRefresher() { Stop(); }

void DashMpdRefresher::Start() {
  if (impl_ != nullptr) {
    const std::shared_ptr<Impl> impl = impl_;
    impl->Post([impl] { impl->Start(); });
  }
}

void DashMpdRefresher::Stop() {
  if (impl_ != nullptr) {
    const std::shared_ptr<Impl> impl = std::move(impl_);
    impl->Post([impl] { impl->Stop(); });
  }
}

}
