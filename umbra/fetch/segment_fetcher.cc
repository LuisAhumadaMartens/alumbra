#include "umbra/fetch/segment_fetcher.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "penumbra/interface/crypto_provider.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/fetch/fetch_failure.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/fetch/init_cache.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/source/url.h"

namespace umbra::fetch {

absl::Status ValidateFetchConfig(const FetchConfig& config) {
  if (config.max_concurrent_requests < 1 ||
      config.max_concurrent_requests > 16) {
    return absl::InvalidArgumentError(
        "max_concurrent_requests must be between 1 and 16");
  }
  if (config.max_concurrent_requests_per_host < 1 ||
      config.max_concurrent_requests_per_host > 8) {
    return absl::InvalidArgumentError(
        "max_concurrent_requests_per_host must be between 1 and 8");
  }
  if (config.max_manifest_requests < 1 || config.max_init_requests < 1 ||
      config.max_media_requests < 1 || config.max_prefetch_requests < 1 ||
      config.max_queued_segments_per_stream < 1) {
    return absl::InvalidArgumentError("every request cap must be positive");
  }
  if (config.init_cache_max_entries < 1 ||
      config.init_cache_max_entries > 256) {
    return absl::InvalidArgumentError(
        "init_cache_max_entries must be between 1 and 256");
  }
  if (config.connect_timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError("connect_timeout must be positive");
  }
  return absl::OkStatus();
}

namespace {

using penumbra::MediaTime;
using penumbra::RequestClass;

constexpr size_t Slot(StreamKind kind) { return static_cast<size_t>(kind); }
constexpr size_t kMaxCachedKeys = 64;

constexpr std::array<StreamKind, kStreamKindCount> kAllKinds = {
    StreamKind::kVideo, StreamKind::kAudio};

enum class ItemKind : uint8_t { kIndex, kKey, kInit, kMedia };
enum class ItemState : uint8_t { kQueued, kInFlight, kDone };

struct Item {
  ItemKind kind = ItemKind::kMedia;
  ItemState state = ItemState::kQueued;
  manifest::TrackId track;
  manifest::Segment segment;
  manifest::InitSection init;
  std::string key_uri;
  std::string key_cache_key;
  std::string required_key_uri;
  std::string required_key_cache_key;
  std::int64_t sequence_number = 0;
  manifest::IndexLocation location;
  std::string url;
  std::string cache_key;
  std::optional<manifest::ByteRange> range;
  uint64_t estimated_bytes = 0;
  uint64_t request_id = 0;
  bool from_cache = false;
  penumbra::SharedBytes bytes;
  bool streamed = false;
};

struct Stream {
  bool selected = false;
  StreamSelection selection;
  manifest::SegmentIndexProvider provider;
  uint64_t generation = 0;
  MediaTime frontier;
  MediaTime delivered_until;
  std::deque<Item> queue;
  std::optional<std::string> last_init_key;
  absl::Status error;
  bool end_reported = false;
  bool waiting = false;
};

struct Request {
  StreamKind kind = StreamKind::kVideo;
  RequestClass request_class = RequestClass::kMedia;
  std::string host;
  std::string url;
  std::string key_uri;
  std::unique_ptr<penumbra::HttpRequestHandle> handle;
  std::optional<uint64_t> expected_length;
  bool expect_partial = false;
  std::vector<penumbra::SharedBytes> parts;
  std::vector<ChunkTiming> chunks;
  std::unique_ptr<penumbra::Aes128CbcDecryptor> decryptor;
  std::vector<std::uint8_t> plaintext;
  bool decrypting = false;
  bool stream_media = false;
  uint64_t bytes = 0;
};

struct CachedKey {
  penumbra::SharedBytes bytes;
};

struct KeyTarget {
  std::string uri;
  std::string cache_key;
};

std::vector<std::uint8_t> SequenceIv(std::int64_t sequence_number) {
  std::vector<std::uint8_t> iv(16, 0);
  auto value = static_cast<std::uint64_t>(sequence_number);
  for (size_t index = 0; index < 8; ++index) {
    iv[15 - index] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8;
  }
  return iv;
}

std::string KeyCacheKey(const std::string& uri,
                        const manifest::KeyReference& reference) {
  constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5',
                                         '6', '7', '8', '9', 'a', 'b',
                                         'c', 'd', 'e', 'f'};
  std::string result = uri;
  result.push_back('|');
  result += std::to_string(static_cast<int>(reference.method));
  result.push_back('|');
  result += reference.key_format;
  result.push_back('|');
  for (const std::uint8_t byte : reference.iv) {
    result.push_back(kHex.at(byte >> 4));
    result.push_back(kHex.at(byte & 0x0f));
  }
  return result;
}

absl::Status MediaCorruptStatus() {
  const base::UrlRedactor redactor;
  return base::MakeError(base::ErrorCode::kMediaCorrupt, redactor);
}

constexpr int ClassRank(RequestClass request_class) {
  switch (request_class) {
    case RequestClass::kManifest:
    case RequestClass::kLicence:
      return 0;
    case RequestClass::kInit:
      return 1;
    case RequestClass::kMedia:
      return 2;
    case RequestClass::kPrefetch:
      return 3;
  }
  return 3;
}

std::string HostOf(const std::string& url) {
  const size_t scheme_end = url.find("://");
  const size_t start = scheme_end == std::string::npos ? 0 : scheme_end + 3;
  size_t end = url.find_first_of("/?#", start);
  if (end == std::string::npos) {
    end = url.size();
  }
  std::string authority = url.substr(start, end - start);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority.erase(0, at + 1);
  }
  return absl::AsciiStrToLower(authority);
}

bool IsHttpsUrl(const std::string& url) {
  return absl::AsciiStrToLower(url).starts_with("https://");
}

bool IsHttpUrl(const std::string& url) {
  return absl::AsciiStrToLower(url).starts_with("http://");
}

absl::StatusOr<std::string> ResolveUrl(const std::string& base,
                                       const std::string& reference) {
  if (base.empty()) {
    auto parsed = source::UrlReference::ParseAbsolute(reference);
    if (!parsed.ok()) {
      return parsed.status();
    }
    return parsed->ToString();
  }
  auto resolved = source::ResolveReference(base, reference);
  if (!resolved.ok()) {
    return resolved.status();
  }
  return resolved->ToString();
}

absl::Status HttpStatusError(int status) {
  const std::string message = absl::StrCat("HTTP status ", status);
  if (status == 401 || status == 403) {
    return absl::PermissionDeniedError(message);
  }
  if (status == 404 || status == 410) {
    return absl::NotFoundError(message);
  }
  if (status == 408 || status == 504) {
    return absl::DeadlineExceededError(message);
  }
  if (status == 429 || status >= 500) {
    return absl::UnavailableError(message);
  }
  return absl::InvalidArgumentError(message);
}

std::optional<size_t> LocateNext(const manifest::SegmentIndex& index,
                                 MediaTime time) {
  if (index.empty()) {
    return std::nullopt;
  }
  if (time < index.Start()) {
    return 0;
  }
  if (const auto found = index.FindByTime(time)) {
    return found;
  }
  if (time >= index.End()) {
    return std::nullopt;
  }
  size_t low = 0;
  size_t high = index.size();
  while (low < high) {
    const size_t middle = low + ((high - low) / 2);
    if (index.StartOf(middle) < time) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low >= index.size()) {
    return std::nullopt;
  }
  return low;
}

uint64_t EstimateBytes(const manifest::Track& track,
                       const manifest::Segment& segment) {
  if (segment.byte_range.has_value() && segment.byte_range->length > 0) {
    return static_cast<uint64_t>(segment.byte_range->length);
  }
  const int64_t bandwidth = track.bandwidth_bps.value_or(0);
  if (!segment.duration.is_finite() || bandwidth <= 0) {
    return 0;
  }
  const double seconds =
      static_cast<double>(segment.duration.InMicroseconds()) / 1e6;
  return static_cast<uint64_t>(static_cast<double>(bandwidth) * seconds / 8.0);
}

void RecomputeFrontier(Stream& stream) {
  MediaTime frontier = stream.delivered_until;
  for (const Item& item : stream.queue) {
    if (item.kind == ItemKind::kMedia) {
      frontier = std::max(frontier, item.segment.End());
    }
  }
  stream.frontier = frontier;
}

penumbra::SharedBytes AssembleBody(Request& request) {
  if (request.parts.size() == 1) {
    return std::move(request.parts.front());
  }
  std::vector<uint8_t> joined;
  joined.reserve(request.bytes);
  for (const penumbra::SharedBytes& part : request.parts) {
    const std::span<const uint8_t> view = part.span();
    joined.insert(joined.end(), view.begin(), view.end());
  }
  return penumbra::SharedBytes::Adopt(std::move(joined));
}

RequestClass ClassFor(const Item& item, const LoadWindow& window) {
  switch (item.kind) {
    case ItemKind::kIndex:
    case ItemKind::kKey:
      return RequestClass::kManifest;
    case ItemKind::kInit:
      return RequestClass::kInit;
    case ItemKind::kMedia:
      break;
  }
  return item.segment.start < window.demand_until ? RequestClass::kMedia
                                                  : RequestClass::kPrefetch;
}

penumbra::RequestPriority PriorityFor(RequestClass request_class) {
  if (request_class == RequestClass::kPrefetch) {
    return penumbra::RequestPriority::kLow;
  }
  if (request_class == RequestClass::kMedia) {
    return penumbra::RequestPriority::kNormal;
  }
  return penumbra::RequestPriority::kHigh;
}

struct QueueTotals {
  int media_items = 0;
  uint64_t bytes = 0;
};

QueueTotals TotalsOf(const Stream& stream) {
  QueueTotals totals;
  for (const Item& queued : stream.queue) {
    if (queued.kind == ItemKind::kMedia) {
      ++totals.media_items;
      totals.bytes += queued.estimated_bytes;
    }
  }
  return totals;
}

}

class SegmentFetcher::Impl : public std::enable_shared_from_this<Impl> {
 public:
  Impl(FetcherDependencies dependencies, const FetchConfig& config)
      : deps_(std::move(dependencies)),
        config_(config),
        cache_(config.init_cache_max_entries) {}

  ~Impl() {
    for (auto& entry : requests_) {
      entry.second.handle->Cancel();
    }
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  absl::Status SelectTrack(StreamKind kind, StreamSelection selection,
                           SwitchMode mode);
  absl::Status UpdateTrack(StreamKind kind,
                           std::shared_ptr<const manifest::Track> track,
                           bool dynamic, std::optional<std::string> base_url);
  void ClearTrack(StreamKind kind);
  uint64_t Seek(MediaTime position);
  void OnBufferChanged();
  void Retry(StreamKind kind);
  [[nodiscard]] LoaderState State(StreamKind kind) const;
  [[nodiscard]] size_t InFlightRequests() const { return requests_.size(); }
  [[nodiscard]] size_t InitCacheSize() const { return cache_.size(); }

  void HandleResponse(uint64_t id, const penumbra::HttpResponseHead& head);
  void HandleChunk(uint64_t id, penumbra::HttpChunk chunk);
  void HandleComplete(uint64_t id, const penumbra::HttpMetrics& metrics);
  void HandleError(uint64_t id, absl::Status status);

 private:
  class Callbacks;

  struct Candidate {
    int rank = 0;
    MediaTime start;
    size_t stream = 0;
    size_t item = 0;
    uint64_t generation = 0;
    RequestClass request_class = RequestClass::kMedia;
  };

  Stream& StreamOf(StreamKind kind) { return streams_.at(Slot(kind)); }
  [[nodiscard]] const Stream& StreamOf(StreamKind kind) const {
    return streams_.at(Slot(kind));
  }

  void Schedule();
  bool DeliverStream(StreamKind kind);
  bool CheckEnd(StreamKind kind);
  bool PlanStream(StreamKind kind);
  bool PlanIndexFetch(StreamKind kind, Stream& stream);
  bool PlanMedia(StreamKind kind, Stream& stream,
                 const manifest::SegmentIndex& index);
  std::vector<Candidate> CollectCandidates() const;
  void DispatchAll();
  absl::Status StartItem(StreamKind kind, Item& item,
                         RequestClass request_class);
  [[nodiscard]] bool CanStart(RequestClass request_class,
                              const std::string& host) const;
  [[nodiscard]] int ClassCap(RequestClass request_class) const;
  void ReleaseRequest(uint64_t id, bool cancel);
  void CancelItem(Item& item);
  void CancelAll(Stream& stream);
  void Fail(StreamKind kind, absl::Status status);
  void FailRequest(uint64_t id, absl::Status status);
  Item* FindItem(StreamKind kind, uint64_t request_id);
  void EmitMediaChunk(StreamKind kind, const Item& item,
                      penumbra::SharedBytes bytes);
  void RecordTransferSample(StreamKind kind, const Request& request,
                            const penumbra::HttpMetrics& metrics) const;
  absl::Status FinishBody(StreamKind kind, Request& request, Item& item);
  void CacheCompletedItem(const Item& item);
  absl::StatusOr<KeyTarget> QueueKey(Stream& stream,
                                     const manifest::SegmentIndex& index,
                                     std::optional<std::uint32_t> key_index,
                                     std::set<std::string>& queued_keys);
  absl::Status QueueMedia(Stream& stream, const manifest::SegmentIndex& index,
                          const manifest::Segment& segment, uint64_t estimate);

  FetcherDependencies deps_;
  FetchConfig config_;
  InitCache cache_;
  std::map<std::string, CachedKey> key_cache_;
  std::array<Stream, kStreamKindCount> streams_;
  std::map<uint64_t, Request> requests_;
  std::map<std::string, uint64_t> pending_key_requests_;
  std::map<std::string, int> host_counts_;
  std::array<int, 5> class_counts_{};
  uint64_t next_request_id_ = 0;
  uint64_t generation_ = 0;
  MediaTime position_;
  bool scheduling_ = false;
  bool dirty_ = false;
  base::SequenceChecker sequence_;
};

class SegmentFetcher::Impl::Callbacks final
    : public penumbra::HttpTransportCallbacks {
 public:
  Callbacks(std::weak_ptr<Impl> impl,
            std::shared_ptr<base::SequencedTaskRunner> runner, uint64_t id)
      : impl_(std::move(impl)), runner_(std::move(runner)), id_(id) {}

  absl::Status OnResponse(penumbra::HttpResponseHead head) override {
    runner_->PostTask(
        [impl = impl_, id = id_, head = std::move(head)]() mutable {
          if (const std::shared_ptr<Impl> target = impl.lock()) {
            target->HandleResponse(id, head);
          }
        });
    return absl::OkStatus();
  }

  absl::Status OnChunk(penumbra::HttpChunk chunk) override {
    runner_->PostTask(
        [impl = impl_, id = id_, chunk = std::move(chunk)]() mutable {
          if (const std::shared_ptr<Impl> target = impl.lock()) {
            target->HandleChunk(id, std::move(chunk));
          }
        });
    return absl::OkStatus();
  }

  absl::Status OnComplete(penumbra::HttpMetrics metrics) override {
    runner_->PostTask(
        [impl = impl_, id = id_, metrics = std::move(metrics)]() mutable {
          if (const std::shared_ptr<Impl> target = impl.lock()) {
            target->HandleComplete(id, metrics);
          }
        });
    return absl::OkStatus();
  }

  absl::Status OnError(absl::Status error) override {
    runner_->PostTask(
        [impl = impl_, id = id_, error = std::move(error)]() mutable {
          if (const std::shared_ptr<Impl> target = impl.lock()) {
            target->HandleError(id, std::move(error));
          }
        });
    return absl::OkStatus();
  }

 private:
  std::weak_ptr<Impl> impl_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  uint64_t id_;
};

absl::Status SegmentFetcher::Impl::SelectTrack(StreamKind kind,
                                               StreamSelection selection,
                                               SwitchMode mode) {
  assert(sequence_.CalledOnValidSequence());
  if (selection.track == nullptr) {
    return absl::InvalidArgumentError("a track is required");
  }
  const penumbra::StreamType expected = kind == StreamKind::kVideo
                                            ? penumbra::StreamType::kVideo
                                            : penumbra::StreamType::kAudio;
  if (selection.track->type() != expected) {
    return absl::InvalidArgumentError("the track does not match the stream");
  }
  Stream& stream = StreamOf(kind);
  if (stream.selected && stream.selection.track->id == selection.track->id) {
    stream.selection.base_url = std::move(selection.base_url);
    return UpdateTrack(kind, std::move(selection.track), selection.dynamic,
                       stream.selection.base_url);
  }
  if (!stream.selected) {
    stream.selected = true;
    stream.delivered_until = position_;
    stream.frontier = position_;
    CancelAll(stream);
  } else if (mode == SwitchMode::kCancelInFlight) {
    CancelAll(stream);
    stream.frontier = stream.delivered_until;
  } else {
    for (auto it = stream.queue.begin(); it != stream.queue.end();) {
      if (it->kind == ItemKind::kIndex || it->state == ItemState::kQueued) {
        CancelItem(*it);
        it = stream.queue.erase(it);
      } else {
        ++it;
      }
    }
    RecomputeFrontier(stream);
  }
  stream.generation = ++generation_;
  stream.selection = std::move(selection);
  stream.provider = stream.selection.track->segments;
  stream.last_init_key.reset();
  stream.error = absl::OkStatus();
  stream.end_reported = false;
  stream.waiting = false;
  Schedule();
  return absl::OkStatus();
}

absl::Status SegmentFetcher::Impl::UpdateTrack(
    StreamKind kind, std::shared_ptr<const manifest::Track> track, bool dynamic,
    std::optional<std::string> base_url) {
  assert(sequence_.CalledOnValidSequence());
  Stream& stream = StreamOf(kind);
  if (!stream.selected || track == nullptr ||
      track->id != stream.selection.track->id) {
    return absl::FailedPreconditionError("the track is not selected");
  }
  stream.selection.track = std::move(track);
  stream.selection.dynamic = dynamic;
  if (base_url.has_value()) {
    stream.selection.base_url = std::move(*base_url);
  }
  const manifest::SegmentIndexProvider& incoming =
      stream.selection.track->segments;
  if (incoming.is_resolved()) {
    for (auto it = stream.queue.begin(); it != stream.queue.end();) {
      if (it->kind == ItemKind::kIndex) {
        CancelItem(*it);
        it = stream.queue.erase(it);
      } else {
        ++it;
      }
    }
    stream.provider = incoming;
  } else if (!stream.provider.is_resolved() ||
             incoming.Location() != stream.provider.Location()) {
    stream.provider = incoming;
  }
  stream.end_reported = false;
  Schedule();
  return absl::OkStatus();
}

void SegmentFetcher::Impl::ClearTrack(StreamKind kind) {
  assert(sequence_.CalledOnValidSequence());
  Stream& stream = StreamOf(kind);
  CancelAll(stream);
  stream = Stream();
  stream.generation = ++generation_;
}

uint64_t SegmentFetcher::Impl::Seek(MediaTime position) {
  assert(sequence_.CalledOnValidSequence());
  position_ = position;
  ++generation_;
  for (const StreamKind kind : kAllKinds) {
    Stream& stream = StreamOf(kind);
    if (!stream.selected) {
      continue;
    }
    CancelAll(stream);
    stream.generation = generation_;
    stream.frontier = position;
    stream.delivered_until = position;
    stream.last_init_key.reset();
    stream.error = absl::OkStatus();
    stream.end_reported = false;
    stream.waiting = false;
  }
  Schedule();
  return generation_;
}

void SegmentFetcher::Impl::OnBufferChanged() {
  assert(sequence_.CalledOnValidSequence());
  Schedule();
}

void SegmentFetcher::Impl::Retry(StreamKind kind) {
  assert(sequence_.CalledOnValidSequence());
  Stream& stream = StreamOf(kind);
  if (!stream.selected || stream.error.ok()) {
    return;
  }
  stream.error = absl::OkStatus();
  stream.frontier = stream.delivered_until;
  stream.last_init_key.reset();
  Schedule();
}

LoaderState SegmentFetcher::Impl::State(StreamKind kind) const {
  const Stream& stream = StreamOf(kind);
  if (!stream.error.ok()) {
    return LoaderState::kError;
  }
  if (!stream.selected) {
    return LoaderState::kIdle;
  }
  if (stream.queue.empty()) {
    return stream.waiting ? LoaderState::kWaitingForAvailability
                          : LoaderState::kIdle;
  }
  switch (stream.queue.front().kind) {
    case ItemKind::kIndex:
    case ItemKind::kKey:
      return LoaderState::kLoadingIndex;
    case ItemKind::kInit:
      return LoaderState::kLoadingInit;
    case ItemKind::kMedia:
      return LoaderState::kLoadingMedia;
  }
  return LoaderState::kIdle;
}

void SegmentFetcher::Impl::Schedule() {
  if (scheduling_) {
    dirty_ = true;
    return;
  }
  scheduling_ = true;
  bool again = true;
  while (again) {
    dirty_ = false;
    again = false;
    for (const StreamKind kind : kAllKinds) {
      again = DeliverStream(kind) || again;
      again = CheckEnd(kind) || again;
    }
    for (const StreamKind kind : kAllKinds) {
      again = PlanStream(kind) || again;
    }
    DispatchAll();
    again = dirty_ || again;
  }
  scheduling_ = false;
}

bool SegmentFetcher::Impl::DeliverStream(StreamKind kind) {
  bool delivered = false;
  for (;;) {
    Stream& stream = StreamOf(kind);
    if (stream.queue.empty() ||
        stream.queue.front().state != ItemState::kDone) {
      return delivered;
    }
    Item item = std::move(stream.queue.front());
    stream.queue.pop_front();
    delivered = true;
    const uint64_t generation = stream.generation;
    switch (item.kind) {
      case ItemKind::kIndex: {
        auto parsed =
            deps_.resolver->ParseIndex(item.location, item.url, item.bytes);
        if (!parsed.ok()) {
          Fail(kind, parsed.status());
          return true;
        }
        stream.provider = stream.provider.WithIndex(*std::move(parsed));
        deps_.sink->OnIndexResolved(kind, stream.selection.track->id,
                                    stream.provider);
        break;
      }
      case ItemKind::kKey:
        break;
      case ItemKind::kInit: {
        InitSegmentData data;
        data.kind = kind;
        data.track = item.track;
        data.generation = generation;
        data.section = std::move(item.init);
        data.bytes = std::move(item.bytes);
        data.from_cache = item.from_cache;
        deps_.sink->OnInitSegment(std::move(data));
        break;
      }
      case ItemKind::kMedia: {
        stream.delivered_until =
            std::max(stream.delivered_until, item.segment.End());
        MediaSegmentData data;
        data.kind = kind;
        data.track = item.track;
        data.generation = generation;
        data.segment = std::move(item.segment);
        data.bytes = std::move(item.bytes);
        if (!item.streamed) {
          deps_.sink->OnMediaSegment(std::move(data));
        }
        break;
      }
    }
    if (StreamOf(kind).generation != generation) {
      return true;
    }
  }
}

bool SegmentFetcher::Impl::CheckEnd(StreamKind kind) {
  Stream& stream = StreamOf(kind);
  if (!stream.selected || !stream.error.ok() || stream.end_reported ||
      stream.selection.dynamic || !stream.provider.is_resolved() ||
      !stream.queue.empty()) {
    return false;
  }
  if (LocateNext(*stream.provider.Index(), stream.frontier).has_value()) {
    return false;
  }
  stream.end_reported = true;
  deps_.sink->OnEndOfStream(kind, stream.generation);
  return true;
}

bool SegmentFetcher::Impl::PlanStream(StreamKind kind) {
  Stream& stream = StreamOf(kind);
  if (!stream.selected || !stream.error.ok() || stream.end_reported) {
    return false;
  }
  const manifest::SegmentIndex* index = stream.provider.Index();
  if (index == nullptr) {
    return PlanIndexFetch(kind, stream);
  }
  return PlanMedia(kind, stream, *index);
}

bool SegmentFetcher::Impl::PlanIndexFetch(StreamKind kind, Stream& stream) {
  const auto& location = stream.provider.Location();
  if (deps_.resolver == nullptr || !location.has_value()) {
    Fail(kind, absl::FailedPreconditionError(
                   "the segment index is deferred and cannot be resolved"));
    return true;
  }
  if (!stream.queue.empty()) {
    return false;
  }
  auto url = ResolveUrl(stream.selection.base_url, location->uri);
  if (!url.ok()) {
    Fail(kind, url.status());
    return true;
  }
  Item item;
  item.kind = ItemKind::kIndex;
  item.track = stream.selection.track->id;
  item.location = *location;
  item.url = *std::move(url);
  item.range = location->byte_range;
  stream.queue.push_back(std::move(item));
  return true;
}

bool SegmentFetcher::Impl::PlanMedia(StreamKind kind, Stream& stream,
                                     const manifest::SegmentIndex& index) {
  stream.waiting = false;
  bool changed = false;
  const LoadWindow window = deps_.policy->WindowFor(kind);
  for (;;) {
    const QueueTotals totals = TotalsOf(stream);
    if (totals.media_items >= config_.max_queued_segments_per_stream) {
      break;
    }
    const auto position = LocateNext(index, stream.frontier);
    if (!position.has_value()) {
      stream.waiting = stream.selection.dynamic;
      break;
    }
    const manifest::Segment segment = index.At(*position);
    if (segment.End() <= stream.frontier) {
      break;
    }
    if (segment.gap) {
      stream.frontier = segment.End();
      continue;
    }
    if (segment.start >= window.prefetch_until) {
      break;
    }
    const uint64_t estimate = EstimateBytes(*stream.selection.track, segment);
    if (totals.bytes > 0 && totals.bytes + estimate > window.byte_budget) {
      break;
    }
    const absl::Status queued = QueueMedia(stream, index, segment, estimate);
    if (!queued.ok()) {
      Fail(kind, queued);
      return true;
    }
    changed = true;
  }
  return changed;
}

absl::StatusOr<KeyTarget> SegmentFetcher::Impl::QueueKey(
    Stream& stream, const manifest::SegmentIndex& index,
    std::optional<std::uint32_t> key_index,
    std::set<std::string>& queued_keys) {
  if (!key_index.has_value()) {
    return KeyTarget{};
  }
  if (*key_index >= index.KeyReferences().size()) {
    return absl::InvalidArgumentError("segment key index is out of range");
  }
  const manifest::KeyReference& reference = index.KeyReferences()[*key_index];
  if (reference.method != manifest::KeyMethod::kAes128 ||
      reference.key_format != "identity") {
    return absl::UnimplementedError("HLS key format is unsupported");
  }
  auto key_url = ResolveUrl(stream.selection.base_url, reference.uri);
  if (!key_url.ok()) {
    return key_url.status();
  }
  if (IsHttpsUrl(stream.selection.base_url) && IsHttpUrl(*key_url) &&
      !config_.allow_insecure_key_uri) {
    return absl::PermissionDeniedError(
        "an HTTPS playlist cannot fetch an HTTP key URI");
  }
  KeyTarget target{.uri = *key_url,
                   .cache_key = KeyCacheKey(*key_url, reference)};
  if (key_cache_.contains(target.cache_key) ||
      queued_keys.contains(target.cache_key)) {
    queued_keys.insert(target.cache_key);
    return target;
  }
  const bool already_queued =
      std::ranges::any_of(stream.queue, [&](const Item& queued) {
        return queued.kind == ItemKind::kKey &&
               queued.key_cache_key == target.cache_key;
      });
  if (!already_queued) {
    Item key;
    key.kind = ItemKind::kKey;
    key.key_uri = target.uri;
    key.key_cache_key = target.cache_key;
    key.url = target.uri;
    stream.queue.push_back(std::move(key));
  }
  queued_keys.insert(target.cache_key);
  return target;
}

absl::Status SegmentFetcher::Impl::QueueMedia(
    Stream& stream, const manifest::SegmentIndex& index,
    const manifest::Segment& segment, uint64_t estimate) {
  std::set<std::string> queued_keys;
  KeyTarget init_key;
  if (segment.init_index.has_value()) {
    if (*segment.init_index >= index.InitSections().size()) {
      return absl::InvalidArgumentError("segment init index is out of range");
    }
    const manifest::InitSection& section =
        index.InitSections()[*segment.init_index];
    auto target = QueueKey(stream, index, section.key_index, queued_keys);
    if (!target.ok()) {
      return target.status();
    }
    init_key = std::move(*target);
  }
  auto media_target = QueueKey(stream, index, segment.key_index, queued_keys);
  if (!media_target.ok()) {
    return media_target.status();
  }
  if (segment.init_index.has_value()) {
    const manifest::InitSection& section =
        index.InitSections()[*segment.init_index];
    auto url = ResolveUrl(stream.selection.base_url, section.uri);
    if (!url.ok()) {
      return url.status();
    }
    std::string key = InitCacheKey(*url, section.byte_range);
    if (!init_key.cache_key.empty()) {
      key += "#hls-key=";
      key += init_key.cache_key;
    }
    if (stream.last_init_key != key) {
      Item init;
      init.kind = ItemKind::kInit;
      init.track = stream.selection.track->id;
      init.init = section;
      init.sequence_number = segment.sequence_number;
      init.required_key_uri = init_key.uri;
      init.required_key_cache_key = init_key.cache_key;
      init.url = *std::move(url);
      init.range = section.byte_range;
      init.cache_key = key;
      if (auto cached = cache_.Find(key)) {
        init.state = ItemState::kDone;
        init.from_cache = true;
        init.bytes = *std::move(cached);
      }
      stream.queue.push_back(std::move(init));
      stream.last_init_key = std::move(key);
    }
  }
  auto url = ResolveUrl(stream.selection.base_url, segment.uri);
  if (!url.ok()) {
    return url.status();
  }
  Item media;
  media.kind = ItemKind::kMedia;
  media.track = stream.selection.track->id;
  media.segment = segment;
  media.required_key_uri = media_target->uri;
  media.required_key_cache_key = media_target->cache_key;
  media.url = *std::move(url);
  media.range = segment.byte_range;
  media.estimated_bytes = estimate;
  stream.frontier = segment.End();
  stream.queue.push_back(std::move(media));
  return absl::OkStatus();
}

std::vector<SegmentFetcher::Impl::Candidate>
SegmentFetcher::Impl::CollectCandidates() const {
  std::vector<Candidate> candidates;
  for (const StreamKind kind : kAllKinds) {
    const Stream& stream = StreamOf(kind);
    if (!stream.selected || !stream.error.ok()) {
      continue;
    }
    const LoadWindow window = deps_.policy->WindowFor(kind);
    for (size_t i = 0; i < stream.queue.size(); ++i) {
      const Item& item = stream.queue.at(i);
      if (item.state != ItemState::kQueued) {
        continue;
      }
      Candidate candidate;
      candidate.stream = Slot(kind);
      candidate.item = i;
      candidate.generation = stream.generation;
      candidate.request_class = ClassFor(item, window);
      candidate.rank = ClassRank(candidate.request_class);
      candidate.start = item.kind == ItemKind::kMedia ? item.segment.start
                                                      : MediaTime::Zero();
      candidates.push_back(candidate);
    }
  }
  std::ranges::stable_sort(candidates,
                           [](const Candidate& a, const Candidate& b) {
                             if (a.rank != b.rank) {
                               return a.rank < b.rank;
                             }
                             if (a.start != b.start) {
                               return a.start < b.start;
                             }
                             return a.stream < b.stream;
                           });
  return candidates;
}

void SegmentFetcher::Impl::DispatchAll() {
  for (const Candidate& candidate : CollectCandidates()) {
    const StreamKind kind = kAllKinds.at(candidate.stream);
    Stream& stream = StreamOf(kind);
    if (stream.generation != candidate.generation || !stream.error.ok() ||
        candidate.item >= stream.queue.size() ||
        stream.queue.at(candidate.item).state != ItemState::kQueued) {
      continue;
    }
    Item& item = stream.queue.at(candidate.item);
    if (item.kind != ItemKind::kKey && !item.required_key_cache_key.empty() &&
        !key_cache_.contains(item.required_key_cache_key)) {
      continue;
    }
    if (item.kind == ItemKind::kKey) {
      const auto cached = key_cache_.find(item.key_cache_key);
      if (cached != key_cache_.end()) {
        item.bytes = cached->second.bytes;
        item.state = ItemState::kDone;
        dirty_ = true;
        continue;
      }
      if (pending_key_requests_.contains(item.key_cache_key)) {
        continue;
      }
    }
    if (!CanStart(candidate.request_class, HostOf(item.url))) {
      continue;
    }
    const absl::Status started = StartItem(kind, item, candidate.request_class);
    if (!started.ok()) {
      Fail(kind, started);
    }
  }
}

int SegmentFetcher::Impl::ClassCap(RequestClass request_class) const {
  switch (request_class) {
    case RequestClass::kManifest:
    case RequestClass::kLicence:
      return config_.max_manifest_requests;
    case RequestClass::kInit:
      return config_.max_init_requests;
    case RequestClass::kMedia:
      return config_.max_media_requests;
    case RequestClass::kPrefetch:
      return config_.max_prefetch_requests;
  }
  return 0;
}

bool SegmentFetcher::Impl::CanStart(RequestClass request_class,
                                    const std::string& host) const {
  if (std::cmp_greater_equal(requests_.size(),
                             config_.max_concurrent_requests)) {
    return false;
  }
  if (class_counts_.at(static_cast<size_t>(request_class)) >=
      ClassCap(request_class)) {
    return false;
  }
  const auto found = host_counts_.find(host);
  return found == host_counts_.end() ||
         found->second < config_.max_concurrent_requests_per_host;
}

absl::Status SegmentFetcher::Impl::StartItem(StreamKind kind, Item& item,
                                             RequestClass request_class) {
  penumbra::HttpRequest request;
  request.url = item.url;
  request.request_class = request_class;
  request.priority = PriorityFor(request_class);
  request.include_credentials = config_.include_credentials;
  request.connect_timeout = config_.connect_timeout;
  Request tracked;
  tracked.kind = kind;
  tracked.request_class = request_class;
  tracked.host = HostOf(item.url);
  tracked.url = item.url;
  if (item.kind == ItemKind::kKey) {
    tracked.key_uri = item.key_cache_key;
  }
  if ((item.kind == ItemKind::kInit || item.kind == ItemKind::kMedia) &&
      !item.required_key_cache_key.empty()) {
    if (deps_.crypto == nullptr) {
      return MediaCorruptStatus();
    }
    const auto key = key_cache_.find(item.required_key_cache_key);
    const manifest::SegmentIndex* index = StreamOf(kind).provider.Index();
    const std::optional<std::uint32_t> key_index = item.kind == ItemKind::kInit
                                                       ? item.init.key_index
                                                       : item.segment.key_index;
    if (key == key_cache_.end() || index == nullptr || !key_index.has_value()) {
      return absl::FailedPreconditionError("the HLS key is unavailable");
    }
    if (key_index.value_or(0) >= index->KeyReferences().size()) {
      return absl::FailedPreconditionError("the HLS key is unavailable");
    }
    const manifest::KeyReference& reference =
        index->KeyReferences()[key_index.value_or(0)];
    const std::vector<std::uint8_t> iv =
        reference.iv.empty() ? SequenceIv(item.kind == ItemKind::kInit
                                              ? item.sequence_number
                                              : item.segment.sequence_number)
                             : reference.iv;
    tracked.decryptor =
        deps_.crypto->CreateAes128CbcDecryptor(key->second.bytes.span(), iv);
    if (tracked.decryptor == nullptr) {
      return MediaCorruptStatus();
    }
    tracked.decrypting = true;
  }
  tracked.stream_media =
      item.kind == ItemKind::kMedia && deps_.sink->SupportsStreamingMedia();
  if (item.range.has_value()) {
    request.range = penumbra::ByteRange{
        .offset = static_cast<uint64_t>(item.range->offset),
        .length = static_cast<uint64_t>(item.range->length)};
    tracked.expect_partial = true;
    tracked.expected_length = static_cast<uint64_t>(item.range->length);
  }
  const uint64_t id = ++next_request_id_;
  auto callbacks =
      std::make_shared<Callbacks>(weak_from_this(), deps_.runner, id);
  auto handle =
      deps_.transport->Start(std::move(request), std::move(callbacks));
  if (!handle.ok()) {
    return handle.status();
  }
  tracked.handle = *std::move(handle);
  ++class_counts_.at(static_cast<size_t>(request_class));
  ++host_counts_[tracked.host];
  requests_.emplace(id, std::move(tracked));
  if (item.kind == ItemKind::kKey) {
    pending_key_requests_.emplace(item.key_cache_key, id);
  }
  item.state = ItemState::kInFlight;
  item.request_id = id;
  return absl::OkStatus();
}

void SegmentFetcher::Impl::ReleaseRequest(uint64_t id, bool cancel) {
  const auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  if (cancel) {
    found->second.handle->Cancel();
  }
  if (!found->second.key_uri.empty()) {
    const auto pending = pending_key_requests_.find(found->second.key_uri);
    if (pending != pending_key_requests_.end() && pending->second == id) {
      pending_key_requests_.erase(pending);
    }
  }
  --class_counts_.at(static_cast<size_t>(found->second.request_class));
  const auto host = host_counts_.find(found->second.host);
  if (host != host_counts_.end() && --host->second == 0) {
    host_counts_.erase(host);
  }
  requests_.erase(found);
}

void SegmentFetcher::Impl::CancelItem(Item& item) {
  if (item.state == ItemState::kInFlight) {
    ReleaseRequest(item.request_id, true);
    item.state = ItemState::kQueued;
  }
}

void SegmentFetcher::Impl::CancelAll(Stream& stream) {
  for (Item& item : stream.queue) {
    CancelItem(item);
  }
  stream.queue.clear();
}

void SegmentFetcher::Impl::Fail(StreamKind kind, absl::Status status) {
  Stream& stream = StreamOf(kind);
  CancelAll(stream);
  stream.error = status;
  stream.waiting = false;
  deps_.sink->OnStreamError(kind, stream.generation, std::move(status));
}

void SegmentFetcher::Impl::FailRequest(uint64_t id, absl::Status status) {
  const auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  if (!GetFetchFailure(status).has_value()) {
    FetchFailure failure;
    failure.url = found->second.url;
    failure.request_class = found->second.request_class;
    status = AttachFetchFailure(std::move(status), failure);
  }
  const StreamKind request_kind = found->second.kind;
  const std::string key_cache_key = found->second.key_uri;
  if (key_cache_key.empty()) {
    Fail(request_kind, std::move(status));
    return;
  }
  bool failed = false;
  for (const StreamKind kind : kAllKinds) {
    const bool waiting_for_key =
        std::ranges::any_of(StreamOf(kind).queue, [&](const Item& item) {
          return item.kind == ItemKind::kKey &&
                 item.key_cache_key == key_cache_key;
        });
    if (waiting_for_key) {
      Fail(kind, status);
      failed = true;
    }
  }
  if (!failed) {
    Fail(request_kind, std::move(status));
  }
}

Item* SegmentFetcher::Impl::FindItem(StreamKind kind, uint64_t request_id) {
  for (Item& item : StreamOf(kind).queue) {
    if (item.state == ItemState::kInFlight && item.request_id == request_id) {
      return &item;
    }
  }
  return nullptr;
}

void SegmentFetcher::Impl::HandleResponse(
    uint64_t id, const penumbra::HttpResponseHead& head) {
  assert(sequence_.CalledOnValidSequence());
  const auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  const bool partial = found->second.expect_partial;
  FetchFailure failure;
  failure.url = found->second.url;
  failure.request_class = found->second.request_class;
  failure.http_status = head.status;
  if (head.status < 200 || head.status > 299) {
    failure.retry_after = ParseRetryAfter(head.headers);
    FailRequest(id, AttachFetchFailure(HttpStatusError(head.status), failure));
    return;
  }
  if (partial && head.status != 206) {
    FailRequest(
        id,
        AttachFetchFailure(
            absl::InvalidArgumentError("the byte range was ignored"), failure));
  }
}

void SegmentFetcher::Impl::HandleChunk(uint64_t id, penumbra::HttpChunk chunk) {
  assert(sequence_.CalledOnValidSequence());
  const auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  Request& request = found->second;
  request.bytes += chunk.data.size();
  request.chunks.push_back(ChunkTiming{.bytes = chunk.data.size(),
                                       .arrival_time = chunk.arrival_time});
  if (request.decrypting) {
    const penumbra::CryptoResult result =
        request.decryptor->Update(chunk.data.span());
    if (!result.ok) {
      FailRequest(id, MediaCorruptStatus());
      return;
    }
    if (request.stream_media) {
      if (!result.bytes.empty()) {
        const Item* item = FindItem(request.kind, id);
        if (item != nullptr) {
          EmitMediaChunk(request.kind, *item,
                         penumbra::SharedBytes::Adopt(result.bytes));
        }
      }
    } else {
      request.plaintext.insert(request.plaintext.end(), result.bytes.begin(),
                               result.bytes.end());
    }
  } else if (request.stream_media) {
    const Item* item = FindItem(request.kind, id);
    if (item != nullptr) {
      EmitMediaChunk(request.kind, *item, std::move(chunk.data));
    }
  } else {
    request.parts.push_back(std::move(chunk.data));
  }
}

void SegmentFetcher::Impl::EmitMediaChunk(StreamKind kind, const Item& item,
                                          penumbra::SharedBytes bytes) {
  deps_.sink->OnMediaChunk(
      MediaSegmentChunkData{.kind = kind,
                            .track = item.track,
                            .generation = StreamOf(kind).generation,
                            .segment = item.segment,
                            .bytes = std::move(bytes)});
}

void SegmentFetcher::Impl::RecordTransferSample(
    StreamKind kind, const Request& request,
    const penumbra::HttpMetrics& metrics) const {
  if (deps_.timing == nullptr ||
      request.request_class == RequestClass::kManifest) {
    return;
  }
  TransferSample sample;
  sample.kind = kind;
  sample.request_class = request.request_class;
  sample.bytes = request.bytes;
  sample.time_to_first_byte = std::max(
      metrics.response_start - metrics.request_start, absl::ZeroDuration());
  sample.transfer_time = std::max(metrics.completion - metrics.response_start,
                                  absl::ZeroDuration());
  sample.coarse_timing = metrics.coarse_timing;
  sample.chunks = request.chunks;
  deps_.timing->OnTransferSample(sample);
}

absl::Status SegmentFetcher::Impl::FinishBody(StreamKind kind, Request& request,
                                              Item& item) {
  if (!request.decrypting) {
    if (!request.stream_media) {
      item.bytes = AssembleBody(request);
    }
    return absl::OkStatus();
  }
  const penumbra::CryptoResult final = request.decryptor->Finalize();
  if (!final.ok) {
    return MediaCorruptStatus();
  }
  if (request.stream_media) {
    if (!final.bytes.empty()) {
      EmitMediaChunk(kind, item, penumbra::SharedBytes::Adopt(final.bytes));
    }
  } else {
    request.plaintext.insert(request.plaintext.end(), final.bytes.begin(),
                             final.bytes.end());
    item.bytes = penumbra::SharedBytes::Adopt(std::move(request.plaintext));
  }
  return absl::OkStatus();
}

void SegmentFetcher::Impl::CacheCompletedItem(const Item& item) {
  if (item.kind == ItemKind::kKey) {
    key_cache_.insert_or_assign(item.key_cache_key,
                                CachedKey{.bytes = item.bytes});
    while (key_cache_.size() > kMaxCachedKeys) {
      key_cache_.erase(key_cache_.begin());
    }
    for (const StreamKind stream_kind : kAllKinds) {
      for (Item& queued : StreamOf(stream_kind).queue) {
        if (queued.kind != ItemKind::kKey ||
            queued.key_cache_key != item.key_cache_key ||
            queued.state != ItemState::kQueued) {
          continue;
        }
        queued.bytes = item.bytes;
        queued.state = ItemState::kDone;
      }
    }
  }
  if (item.kind == ItemKind::kInit) {
    cache_.Insert(item.cache_key, item.bytes);
  }
}

void SegmentFetcher::Impl::HandleComplete(
    uint64_t id, const penumbra::HttpMetrics& metrics) {
  assert(sequence_.CalledOnValidSequence());
  const auto found = requests_.find(id);
  if (found == requests_.end()) {
    return;
  }
  Request& request = found->second;
  const StreamKind kind = request.kind;
  Item* item = FindItem(kind, id);
  if (item == nullptr) {
    ReleaseRequest(id, false);
    return;
  }
  const bool wrong_size =
      request.bytes == 0 ||
      (request.expected_length.has_value() &&
       request.expected_length.value_or(0) != request.bytes);
  if (wrong_size) {
    FailRequest(id,
                absl::DataLossError("the response body has the wrong size"));
    return;
  }
  if (item->kind == ItemKind::kKey && request.bytes != 16) {
    FailRequest(id, absl::DataLossError("the HLS key must contain 16 bytes"));
    return;
  }
  RecordTransferSample(kind, request, metrics);
  const absl::Status finish_status = FinishBody(kind, request, *item);
  if (!finish_status.ok()) {
    FailRequest(id, finish_status);
    return;
  }
  if (request.stream_media) {
    item->streamed = true;
    deps_.sink->OnMediaSegmentEnd(
        MediaSegmentEndData{.kind = kind,
                            .track = item->track,
                            .generation = StreamOf(kind).generation,
                            .segment = item->segment});
  }
  item->state = ItemState::kDone;
  CacheCompletedItem(*item);
  ReleaseRequest(id, false);
  Schedule();
}

void SegmentFetcher::Impl::HandleError(uint64_t id, absl::Status status) {
  assert(sequence_.CalledOnValidSequence());
  FailRequest(id, std::move(status));
}

absl::StatusOr<std::unique_ptr<SegmentFetcher>> SegmentFetcher::Create(
    FetcherDependencies dependencies, const FetchConfig& config) {
  if (absl::Status valid = ValidateFetchConfig(config); !valid.ok()) {
    return valid;
  }
  if (dependencies.transport == nullptr || dependencies.runner == nullptr ||
      dependencies.sink == nullptr || dependencies.policy == nullptr) {
    return absl::InvalidArgumentError(
        "transport, runner, sink and policy are required");
  }
  return std::unique_ptr<SegmentFetcher>(new SegmentFetcher(
      std::make_shared<Impl>(std::move(dependencies), config)));
}

SegmentFetcher::SegmentFetcher(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SegmentFetcher::~SegmentFetcher() = default;

absl::Status SegmentFetcher::SelectTrack(StreamKind kind,
                                         StreamSelection selection,
                                         SwitchMode mode) {
  return impl_->SelectTrack(kind, std::move(selection), mode);
}

absl::Status SegmentFetcher::UpdateTrack(
    StreamKind kind, std::shared_ptr<const manifest::Track> track, bool dynamic,
    std::optional<std::string> base_url) {
  return impl_->UpdateTrack(kind, std::move(track), dynamic,
                            std::move(base_url));
}

void SegmentFetcher::ClearTrack(StreamKind kind) { impl_->ClearTrack(kind); }

uint64_t SegmentFetcher::Seek(penumbra::MediaTime position) {
  return impl_->Seek(position);
}

void SegmentFetcher::OnBufferChanged() { impl_->OnBufferChanged(); }

void SegmentFetcher::Retry(StreamKind kind) { impl_->Retry(kind); }

LoaderState SegmentFetcher::State(StreamKind kind) const {
  return impl_->State(kind);
}

size_t SegmentFetcher::InFlightRequests() const {
  return impl_->InFlightRequests();
}

size_t SegmentFetcher::InitCacheSize() const { return impl_->InitCacheSize(); }

}
