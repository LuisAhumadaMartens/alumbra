#ifndef PENUMBRA_DESKTOP_WORKER_DECODER_H_
#define PENUMBRA_DESKTOP_WORKER_DECODER_H_

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

using EventDetail = decltype(DecoderEvent::detail);

class WorkerDecoder : public Decoder {
 public:
  WorkerDecoder(const WorkerDecoder&) = delete;
  WorkerDecoder& operator=(const WorkerDecoder&) = delete;
  ~WorkerDecoder() override;

  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples) final;
  absl::Status EndOfStream(SeekTicket ticket) final;
  absl::Status Flush(SeekTicket ticket) final;
  absl::Status Stop() final;

 protected:
  WorkerDecoder(StreamInfo info, SeekTicket ticket,
                std::shared_ptr<DecoderCallbacks> callbacks,
                size_t grant_samples);

  void Start();

  virtual absl::Status Decode(const EncodedSample& sample) = 0;
  virtual absl::Status Drain() = 0;
  virtual void Reset() = 0;
  [[nodiscard]] virtual absl::Status CheckConfigChange(
      const StreamInfo& from, const StreamInfo& to) const = 0;

  absl::Status Emit(std::variant<DecodedVideoFrame, DecodedAudioBuffer> output);

 private:
  void Run();
  absl::Status Deliver(SeekTicket ticket, EventDetail detail);

  std::shared_ptr<DecoderCallbacks> callbacks_;
  const uint32_t track_id_;
  const size_t grant_samples_;

  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  std::deque<EncodedSample> queue_;
  StreamInfo info_;
  SeekTicket ticket_;
  size_t outstanding_grant_ = 0;
  bool grant_pending_ = true;
  bool eos_requested_ = false;
  bool eos_done_ = false;
  bool failed_ = false;
  bool stopped_ = false;
  bool busy_ = false;
  SeekTicket active_ticket_;
  std::thread thread_;
};

}

#endif
