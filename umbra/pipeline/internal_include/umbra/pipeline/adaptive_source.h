#ifndef UMBRA_PIPELINE_ADAPTIVE_SOURCE_H_
#define UMBRA_PIPELINE_ADAPTIVE_SOURCE_H_

#include <functional>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "umbra/base/task_runner.h"
#include "umbra/fetch/fetch_types.h"
#include "umbra/manifest/presentation.h"
#include "umbra/pipeline/buffer_manager.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline {

struct AdaptiveSourceConfig {
  std::shared_ptr<const manifest::Presentation> presentation;
  std::string manifest_url;
  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<fetch::IndexResolver> index_resolver;
  std::shared_ptr<fetch::TransferTimingSink> transfer_timing;
  std::shared_ptr<fetch::LoadPolicy> load_policy;
  BufferManager::Config buffer;
  fetch::FetchConfig fetch;
  penumbra::MediaTime initial_position = penumbra::MediaTime::Zero();
  std::string dash_default_utc_timing_url;
  std::int64_t dash_start_delay_ms = -1;
  std::function<absl::Time()> wall_clock;
};

class AdaptiveSource final : public SampleSource {
 public:
  static absl::StatusOr<std::unique_ptr<AdaptiveSource>> Create(
      AdaptiveSourceConfig config);

  ~AdaptiveSource() override;
  AdaptiveSource(const AdaptiveSource&) = delete;
  AdaptiveSource& operator=(const AdaptiveSource&) = delete;

  void Open(std::shared_ptr<SampleSink> sink) override;
  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) override;
  [[nodiscard]] absl::Status SelectRendition(fetch::StreamKind kind,
                                             const manifest::TrackId& track);
  [[nodiscard]] absl::Status UpdatePresentation(
      std::shared_ptr<const manifest::Presentation> presentation);
  [[nodiscard]] SeekPoint ResolveSeek(penumbra::MediaTime position,
                                      SeekMode mode,
                                      FastSeekSnap snap) const override;
  void Seek(penumbra::SeekTicket ticket,
            penumbra::MediaTime decode_position) override;
  void Close() override;

 private:
  class Impl;
  explicit AdaptiveSource(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

}

#endif
