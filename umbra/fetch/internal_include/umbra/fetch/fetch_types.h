#ifndef UMBRA_FETCH_FETCH_TYPES_H_
#define UMBRA_FETCH_FETCH_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/crypto_provider.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"

namespace umbra::fetch {

enum class StreamKind : uint8_t { kVideo, kAudio };

inline constexpr size_t kStreamKindCount = 2;

enum class LoaderState : uint8_t {
  kIdle,
  kLoadingIndex,
  kLoadingInit,
  kLoadingMedia,
  kWaitingForAvailability,
  kError,
};

enum class SwitchMode : uint8_t { kAfterInFlight, kCancelInFlight };

struct FetchConfig {
  int max_concurrent_requests = 6;
  int max_concurrent_requests_per_host = 6;
  int max_manifest_requests = 2;
  int max_init_requests = 2;
  int max_media_requests = 4;
  int max_prefetch_requests = 2;
  int max_queued_segments_per_stream = 8;
  size_t init_cache_max_entries = 16;
  bool include_credentials = false;
  bool allow_insecure_key_uri = false;
  absl::Duration connect_timeout = absl::Seconds(10);
};

[[nodiscard]] absl::Status ValidateFetchConfig(const FetchConfig& config);

struct LoadWindow {
  penumbra::MediaTime demand_until;
  penumbra::MediaTime prefetch_until;
  uint64_t byte_budget = std::numeric_limits<uint64_t>::max();
};

class LoadPolicy {
 public:
  LoadPolicy() = default;
  LoadPolicy(const LoadPolicy&) = delete;
  LoadPolicy& operator=(const LoadPolicy&) = delete;
  virtual ~LoadPolicy() = default;

  [[nodiscard]] virtual LoadWindow WindowFor(StreamKind kind) const = 0;
};

struct InitSegmentData {
  StreamKind kind = StreamKind::kVideo;
  manifest::TrackId track;
  uint64_t generation = 0;
  manifest::InitSection section;
  penumbra::SharedBytes bytes;
  bool from_cache = false;
};

struct MediaSegmentData {
  StreamKind kind = StreamKind::kVideo;
  manifest::TrackId track;
  uint64_t generation = 0;
  manifest::Segment segment;
  penumbra::SharedBytes bytes;
};

struct MediaSegmentChunkData {
  StreamKind kind = StreamKind::kVideo;
  manifest::TrackId track;
  uint64_t generation = 0;
  manifest::Segment segment;
  penumbra::SharedBytes bytes;
};

struct MediaSegmentEndData {
  StreamKind kind = StreamKind::kVideo;
  manifest::TrackId track;
  uint64_t generation = 0;
  manifest::Segment segment;
};

class SegmentSink {
 public:
  SegmentSink() = default;
  SegmentSink(const SegmentSink&) = delete;
  SegmentSink& operator=(const SegmentSink&) = delete;
  virtual ~SegmentSink() = default;

  virtual void OnInitSegment(InitSegmentData init) = 0;
  virtual void OnMediaSegment(MediaSegmentData media) = 0;
  [[nodiscard]] virtual bool SupportsStreamingMedia() const { return false; }
  virtual void OnMediaChunk(const MediaSegmentChunkData& data) {
    static_cast<void>(data);
  }
  virtual void OnMediaSegmentEnd(const MediaSegmentEndData& data) {
    static_cast<void>(data);
  }
  virtual void OnEndOfStream(StreamKind kind, uint64_t generation) = 0;
  virtual void OnStreamError(StreamKind kind, uint64_t generation,
                             absl::Status status) = 0;
  virtual void OnIndexResolved(
      StreamKind kind, const manifest::TrackId& track,
      const manifest::SegmentIndexProvider& provider) = 0;
};

struct ChunkTiming {
  uint64_t bytes = 0;
  absl::Duration arrival_time;
};

struct TransferSample {
  StreamKind kind = StreamKind::kVideo;
  penumbra::RequestClass request_class = penumbra::RequestClass::kMedia;
  uint64_t bytes = 0;
  absl::Duration time_to_first_byte;
  absl::Duration transfer_time;
  bool coarse_timing = false;
  std::vector<ChunkTiming> chunks;
};

class TransferTimingSink {
 public:
  TransferTimingSink() = default;
  TransferTimingSink(const TransferTimingSink&) = delete;
  TransferTimingSink& operator=(const TransferTimingSink&) = delete;
  virtual ~TransferTimingSink() = default;

  virtual void OnTransferSample(const TransferSample& sample) = 0;
};

class IndexResolver {
 public:
  IndexResolver() = default;
  IndexResolver(const IndexResolver&) = delete;
  IndexResolver& operator=(const IndexResolver&) = delete;
  virtual ~IndexResolver() = default;

  [[nodiscard]] virtual absl::StatusOr<
      std::shared_ptr<const manifest::SegmentIndex>>
  ParseIndex(const manifest::IndexLocation& location, const std::string& url,
             const penumbra::SharedBytes& bytes) = 0;
};

}

#endif
