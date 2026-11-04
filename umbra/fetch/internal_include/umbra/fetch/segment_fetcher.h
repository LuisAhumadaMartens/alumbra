#ifndef UMBRA_FETCH_SEGMENT_FETCHER_H_
#define UMBRA_FETCH_SEGMENT_FETCHER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/media_time.h"
#include "umbra/base/task_runner.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/manifest/presentation.h"

namespace umbra::fetch {

struct StreamSelection {
  std::shared_ptr<const manifest::Track> track;
  std::string base_url;
  bool dynamic = false;
};

struct FetcherDependencies {
  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<penumbra::CryptoProvider> crypto;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<SegmentSink> sink;
  std::shared_ptr<LoadPolicy> policy;
  std::shared_ptr<TransferTimingSink> timing;
  std::shared_ptr<IndexResolver> resolver;
};

class SegmentFetcher {
 public:
  static absl::StatusOr<std::unique_ptr<SegmentFetcher>> Create(
      FetcherDependencies dependencies, const FetchConfig& config);

  ~SegmentFetcher();
  SegmentFetcher(const SegmentFetcher&) = delete;
  SegmentFetcher& operator=(const SegmentFetcher&) = delete;

  absl::Status SelectTrack(StreamKind kind, StreamSelection selection,
                           SwitchMode mode);
  absl::Status UpdateTrack(StreamKind kind,
                           std::shared_ptr<const manifest::Track> track,
                           bool dynamic,
                           std::optional<std::string> base_url = std::nullopt);
  void ClearTrack(StreamKind kind);
  uint64_t Seek(penumbra::MediaTime position);
  void OnBufferChanged();
  void Retry(StreamKind kind);

  [[nodiscard]] LoaderState State(StreamKind kind) const;
  [[nodiscard]] size_t InFlightRequests() const;
  [[nodiscard]] size_t InitCacheSize() const;

 private:
  class Impl;

  explicit SegmentFetcher(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

}

#endif
