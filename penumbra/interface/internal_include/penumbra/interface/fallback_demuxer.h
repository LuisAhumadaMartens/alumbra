#ifndef PENUMBRA_INTERFACE_FALLBACK_DEMUXER_H_
#define PENUMBRA_INTERFACE_FALLBACK_DEMUXER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {

struct FallbackProbe {
  bool supported = false;
  std::string container;
};

struct FallbackDemuxerRequest {
  SeekTicket initial_ticket = 1;
  std::string name_hint;
};

struct FallbackDemuxerInfo {
  std::string container;
  std::vector<StreamInfo> streams;
  TimeDelta duration = TimeDelta::Unknown();
  bool seekable = false;
};

class FallbackByteSource {
 public:
  virtual ~FallbackByteSource() = default;
  virtual absl::StatusOr<size_t> ReadAt(uint64_t offset,
                                        std::span<uint8_t> destination) = 0;
  [[nodiscard]] virtual std::optional<uint64_t> Size() const = 0;
  [[nodiscard]] virtual bool Seekable() const = 0;
};

class FallbackDemuxerCallbacks {
 public:
  virtual ~FallbackDemuxerCallbacks() = default;
  virtual absl::Status OnOpen(FallbackDemuxerInfo info) = 0;
  virtual absl::Status OnSamples(SeekTicket ticket, uint32_t track_id,
                                 std::vector<EncodedSample> samples) = 0;
  virtual absl::Status OnEndOfStream(SeekTicket ticket, uint32_t track_id) = 0;
  virtual absl::Status OnError(absl::Status error) = 0;
};

class FallbackDemuxer {
 public:
  virtual ~FallbackDemuxer() = default;
  virtual absl::Status RequestSamples(SeekTicket ticket, uint32_t track_id,
                                      size_t max_samples) = 0;
  virtual absl::Status Seek(SeekTicket ticket, MediaTime position) = 0;
  virtual absl::Status Stop() = 0;
};

class FallbackDemuxerProvider {
 public:
  virtual ~FallbackDemuxerProvider() = default;
  [[nodiscard]] virtual absl::StatusOr<FallbackProbe> ProbeFallbackDemuxer(
      std::span<const uint8_t> window, std::string_view name_hint) const = 0;
  virtual absl::StatusOr<std::unique_ptr<FallbackDemuxer>> OpenFallbackDemuxer(
      FallbackDemuxerRequest request,
      std::shared_ptr<FallbackByteSource> source,
      std::shared_ptr<FallbackDemuxerCallbacks> callbacks) = 0;
};

}

#endif
