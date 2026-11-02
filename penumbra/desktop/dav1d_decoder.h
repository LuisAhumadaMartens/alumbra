#ifndef PENUMBRA_DESKTOP_DAV1D_DECODER_H_
#define PENUMBRA_DESKTOP_DAV1D_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/worker_decoder.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

struct Dav1dContext;
struct Dav1dData;

namespace penumbra::desktop {

struct Dav1dContextDeleter {
  void operator()(Dav1dContext* context) const;
};

struct Dav1dDecoderOptions {
  int32_t threads = 0;
  bool apply_film_grain = true;
  int32_t max_frame_delay = 0;
  int32_t operating_point = 0;
};

class Dav1dDecoder final : public WorkerDecoder {
 public:
  static constexpr int32_t kMaxThreads = 64;
  static constexpr int32_t kMaxFrameDelay = 256;
  static constexpr int32_t kMaxOperatingPoint = 31;

  static absl::StatusOr<std::unique_ptr<Decoder>> Create(
      StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks,
      const Dav1dDecoderOptions& options);

  Dav1dDecoder(StreamInfo info, SeekTicket ticket,
               std::shared_ptr<DecoderCallbacks> callbacks,
               size_t grant_samples);
  ~Dav1dDecoder() override;

 protected:
  absl::Status Decode(const EncodedSample& sample) override;
  absl::Status Drain() override;
  void Reset() override;
  [[nodiscard]] absl::Status CheckConfigChange(
      const StreamInfo& from, const StreamInfo& to) const override;

 private:
  absl::Status Open(const StreamConfig& config,
                    const Dav1dDecoderOptions& options);
  absl::Status Feed(Dav1dData& data);
  absl::Status FeedBytes(const SharedBytes& bytes, int64_t timestamp,
                         int64_t duration);
  absl::Status TakePicture(bool& produced);
  void Resynchronise();
  [[nodiscard]] int64_t ToStreamTicks(Timestamp timestamp) const;

  std::unique_ptr<Dav1dContext, Dav1dContextDeleter> context_;
  Rational time_base_;
  Rational frame_rate_;
  std::vector<uint8_t> config_obus_;
};

}

#endif
