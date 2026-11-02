#ifndef UMBRA_PIPELINE_SAMPLE_SOURCE_H_
#define UMBRA_PIPELINE_SAMPLE_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/error_detail.h"

namespace umbra::pipeline {

struct SourceDescription {
  std::vector<penumbra::StreamConfig> streams;
  penumbra::MediaTime duration = penumbra::MediaTime::Unknown();
};

class SampleSink {
 public:
  virtual ~SampleSink() = default;
  virtual void OnOpened(SourceDescription description) = 0;
  virtual void OnOpenFailed(base::ErrorDetail detail) = 0;
  virtual void OnDurationChanged(penumbra::MediaTime duration) = 0;
  virtual void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                         std::vector<penumbra::EncodedSample> samples) = 0;
  virtual void OnEndOfStream(penumbra::SeekTicket ticket,
                             uint32_t track_id) = 0;
  virtual void OnSourceError(base::ErrorDetail detail) = 0;
};

class SampleSource {
 public:
  virtual ~SampleSource() = default;
  virtual void Open(std::shared_ptr<SampleSink> sink) = 0;
  virtual void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                              size_t max_samples) = 0;
  virtual void Seek(penumbra::SeekTicket ticket,
                    penumbra::MediaTime position) = 0;
  virtual void Close() = 0;
};

}

#endif
