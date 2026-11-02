#include "fake_sample_source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/error_detail.h"
#include "umbra/pipeline/sample_source.h"

namespace umbra::pipeline::test {

void SourceControl::CompleteOpen() {
  for (const auto& stream : description.streams) {
    cursor_ms[stream.info.track_id] = 0;
  }
  sink->OnOpened(description);
}

void SourceControl::FailOpen(base::ErrorDetail detail) const {
  sink->OnOpenFailed(std::move(detail));
}

void SourceControl::Release() {
  held = false;
  const std::vector<Request> pending = std::move(queued);
  queued.clear();
  for (const Request& request : pending) {
    Deliver(request);
  }
}

void SourceControl::Deliver(const Request& request) {
  const int64_t total_ms = description.duration.InMicroseconds() / 1000;
  int64_t& cursor = cursor_ms[request.track_id];
  if (cursor >= total_ms) {
    sink->OnEndOfStream(request.ticket, request.track_id);
    return;
  }
  std::vector<penumbra::EncodedSample> samples;
  while (samples.size() < request.max_samples && cursor < total_ms) {
    penumbra::EncodedSample sample;
    sample.track_id = request.track_id;
    sample.data = penumbra::SharedBytes::Adopt({1, 2, 3, 4});
    sample.pts = {.value = cursor,
                  .time_base = penumbra::Rational::Of(1, 1000)};
    sample.duration = {.value = sample_ms,
                       .time_base = penumbra::Rational::Of(1, 1000)};
    sample.key_frame = true;
    samples.push_back(std::move(sample));
    cursor += sample_ms;
  }
  sink->OnSamples(request.ticket, request.track_id, std::move(samples));
}

void FakeSampleSource::Open(std::shared_ptr<SampleSink> sink) {
  control_->sink = std::move(sink);
  control_->opened = true;
}

void FakeSampleSource::RequestSamples(penumbra::SeekTicket ticket,
                                      uint32_t track_id, size_t max_samples) {
  const SourceControl::Request request{
      .ticket = ticket, .track_id = track_id, .max_samples = max_samples};
  control_->requests.push_back(request);
  if (control_->held) {
    control_->queued.push_back(request);
    return;
  }
  control_->Deliver(request);
}

void FakeSampleSource::Seek(penumbra::SeekTicket ticket,
                            penumbra::MediaTime position) {
  static_cast<void>(ticket);
  control_->seeks.push_back(position);
  for (auto& [track, cursor] : control_->cursor_ms) {
    cursor = position.InMicroseconds() / 1000;
  }
}

void FakeSampleSource::Close() { control_->closed = true; }

}
