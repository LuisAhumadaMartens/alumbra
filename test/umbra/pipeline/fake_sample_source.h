#ifndef TEST_UMBRA_PIPELINE_FAKE_SAMPLE_SOURCE_H_
#define TEST_UMBRA_PIPELINE_FAKE_SAMPLE_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/error_detail.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline::test {

struct SourceControl {
  struct Request {
    penumbra::SeekTicket ticket = 0;
    uint32_t track_id = 0;
    size_t max_samples = 0;
  };

  SourceDescription description;
  int64_t sample_ms = 100;
  bool opened = false;
  bool closed = false;
  bool held = false;
  std::shared_ptr<SampleSink> sink;
  std::vector<Request> queued;
  std::vector<Request> requests;
  std::vector<penumbra::MediaTime> seeks;
  std::map<uint32_t, int64_t> cursor_ms;

  void CompleteOpen();
  void FailOpen(base::ErrorDetail detail) const;
  void Hold() { held = true; }
  void Release();
  void Deliver(const Request& request);
};

class FakeSampleSource final : public SampleSource {
 public:
  explicit FakeSampleSource(std::shared_ptr<SourceControl> control)
      : control_(std::move(control)) {}
  void Open(std::shared_ptr<SampleSink> sink) override;
  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) override;
  void Seek(penumbra::SeekTicket ticket, penumbra::MediaTime position) override;
  void Close() override;

 private:
  std::shared_ptr<SourceControl> control_;
};

}

#endif
