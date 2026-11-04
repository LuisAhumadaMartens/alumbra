#ifndef UMBRA_PIPELINE_PROGRESSIVE_SOURCE_H_
#define UMBRA_PIPELINE_PROGRESSIVE_SOURCE_H_

#include <memory>

#include "absl/status/statusor.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/data_source.h"
#include "umbra/source/source_type.h"

namespace umbra::pipeline {

class ProgressiveSource final : public SampleSource {
 public:
  static absl::StatusOr<std::unique_ptr<ProgressiveSource>> Create(
      std::shared_ptr<source::DataSource> data_source,
      std::shared_ptr<base::SequencedTaskRunner> runner,
      std::shared_ptr<const base::UrlRedactor> redactor,
      source::SniffHints hints = {},
      source::BufferedReaderOptions reader_options = {});

  ~ProgressiveSource() override;
  ProgressiveSource(const ProgressiveSource&) = delete;
  ProgressiveSource& operator=(const ProgressiveSource&) = delete;

  void Open(std::shared_ptr<SampleSink> sink) override;
  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) override;
  [[nodiscard]] SeekPoint ResolveSeek(penumbra::MediaTime position,
                                      SeekMode mode,
                                      FastSeekSnap snap) const override;
  void Seek(penumbra::SeekTicket ticket,
            penumbra::MediaTime decode_position) override;
  void Close() override;

 private:
  class Impl;
  explicit ProgressiveSource(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

}

#endif
